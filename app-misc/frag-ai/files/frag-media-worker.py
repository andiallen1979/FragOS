#!/usr/bin/env python3
import json, os, subprocess, sys, time, urllib.request

OLLAMA_URL = "http://localhost:11434"
KOKORO_DIR = os.path.expanduser("~/.cache/frag-ai/kokoro")

def set_status(job_dir, stage, detail="", done=False, error="", outputs=None):
    p = os.path.join(job_dir, "status.json")
    cur = json.load(open(p)) if os.path.exists(p) else {}
    cur.update({"stage": stage, "detail": detail, "done": done, "error": error, "updated": time.time()})
    if outputs is not None:
        cur["outputs"] = outputs
    json.dump(cur, open(p, "w"), indent=2)

def flush_ollama_vram():
    try:
        ps = json.loads(urllib.request.urlopen(f"{OLLAMA_URL}/api/ps", timeout=5).read().decode())
        for m in ps.get("models", []):
            req = urllib.request.Request(
                f"{OLLAMA_URL}/api/generate",
                data=json.dumps({"model": m["name"], "keep_alive": 0}).encode(),
                headers={"Content-Type": "application/json"},
            )
            urllib.request.urlopen(req, timeout=10).read()
    except Exception:
        pass

def pick_ollama_model(requested):
    if requested:
        return requested
    tags = json.loads(urllib.request.urlopen(f"{OLLAMA_URL}/api/tags", timeout=10).read().decode()).get("models", [])
    if not tags:
        raise RuntimeError("No local Ollama models installed.")
    return tags[0]["name"]

def do_research(query):
    try:
        from ddgs import DDGS
        results = DDGS().text(query, max_results=5)
        return "\n".join(f"- {r.get('title','')}: {r.get('body','')}" for r in results)
    except Exception as e:
        return f"(Web search skipped: {e})"

def ensure_kokoro():
    os.makedirs(KOKORO_DIR, exist_ok=True)
    base = "https://github.com/thewh1teagle/kokoro-onnx/releases/download/model-files-v1.0"
    files = {
        "model": (os.path.join(KOKORO_DIR, "kokoro-v1.0.onnx"), f"{base}/kokoro-v1.0.onnx"),
        "voices": (os.path.join(KOKORO_DIR, "voices-v1.0.bin"), f"{base}/voices-v1.0.bin"),
    }
    for path, url in files.values():
        if not os.path.exists(path):
            urllib.request.urlretrieve(url, path)
    return files["model"][0], files["voices"][0]

def main():
    job_dir = sys.argv[1]
    job = json.load(open(os.path.join(job_dir, "job.json")))
    prompt = job["prompt"]
    mode = job.get("mode", "video")
    outputs = {"images": [], "audio": [], "video": "", "script": ""}

    try:
        research_txt = ""
        if job.get("research", True):
            set_status(job_dir, "researching", "Searching web for context...")
            research_txt = do_research(prompt)
            open(os.path.join(job_dir, "research.txt"), "w").write(research_txt)

        set_status(job_dir, "directing", "Writing script & shotlist via Ollama...")
        model = pick_ollama_model(job.get("llm", ""))
        sys_msg = (
            "You are a media director. Return ONLY valid JSON with this schema: "
            '{"title":"short title","summary":"2-3 sentence summary","scenes":[{"narration":"spoken voiceover text","image_prompt":"detailed visual prompt for SDXL, no text overlays"}]} '
            f"Create {'1 scene' if mode == 'image' else '3 concise scenes'}."
        )
        user_msg = f"Request: {prompt}\n\nResearch Notes:\n{research_txt[:2500]}"
        req = urllib.request.Request(
            f"{OLLAMA_URL}/api/chat",
            data=json.dumps({
                "model": model, "format": "json", "stream": False,
                "messages": [{"role": "system", "content": sys_msg}, {"role": "user", "content": user_msg}]
            }).encode(),
            headers={"Content-Type": "application/json"},
        )
        resp = json.loads(urllib.request.urlopen(req, timeout=180).read().decode())
        plan = json.loads(resp["message"]["content"])
        json.dump(plan, open(os.path.join(job_dir, "plan.json"), "w"), indent=2)
        outputs["script"] = "plan.json"

        if mode == "research":
            set_status(job_dir, "done", "Research & script complete.", done=True, outputs=outputs)
            return

        set_status(job_dir, "unloading_llm", "Flushing Ollama from 8GB VRAM...")
        flush_ollama_vram()

        scenes = plan.get("scenes") or [{"narration": prompt, "image_prompt": prompt}]

        if mode in ("video", "audio"):
            set_status(job_dir, "audio", "Synthesizing voiceover on CPU (Kokoro-ONNX)...")
            import soundfile as sf
            from kokoro_onnx import Kokoro
            m_path, v_path = ensure_kokoro()
            kokoro = Kokoro(m_path, v_path)
            voice = job.get("voice", "af_heart")
            for i, sc in enumerate(scenes):
                text = (sc.get("narration") or "").strip()
                if not text:
                    continue
                samples, sr = kokoro.create(text, voice=voice, speed=1.0, lang="en-us")
                wav_name = f"scene_{i}.wav"
                sf.write(os.path.join(job_dir, wav_name), samples, sr)
                outputs["audio"].append(wav_name)

        if mode in ("video", "image"):
            set_status(job_dir, "images", "Loading SDXL-Turbo (8GB VRAM safe offload)...")
            import torch
            from diffusers import AutoPipelineForText2Image
            img_model = job.get("img_model") or "stabilityai/sdxl-turbo"
            pipe = AutoPipelineForText2Image.from_pretrained(img_model, torch_dtype=torch.float16, variant="fp16")
            if torch.cuda.is_available():
                pipe.enable_model_cpu_offload()
            else:
                pipe.to("cpu")
            pipe.vae.enable_slicing()
            for i, sc in enumerate(scenes):
                set_status(job_dir, "images", f"Rendering image {i+1}/{len(scenes)}...")
                img = pipe(prompt=sc.get("image_prompt", prompt), num_inference_steps=4, guidance_scale=0.0, width=1024, height=576).images[0]
                png_name = f"scene_{i}.png"
                img.save(os.path.join(job_dir, png_name))
                outputs["images"].append(png_name)
            del pipe
            torch.cuda.empty_cache()

        if mode == "video":
            set_status(job_dir, "video", "Rendering & muxing MP4 with FFmpeg...")
            clips = []
            for i in range(len(outputs["images"])):
                img_p = os.path.join(job_dir, f"scene_{i}.png")
                wav_p = os.path.join(job_dir, f"scene_{i}.wav")
                clip_p = os.path.join(job_dir, f"clip_{i}.mp4")
                cmd = ["ffmpeg", "-y", "-loop", "1", "-i", img_p]
                if os.path.exists(wav_p):
                    cmd += ["-i", wav_p, "-c:a", "aac", "-shortest"]
                else:
                    cmd += ["-t", "5"]
                cmd += [
                    "-vf", "scale=1280:720:force_original_aspect_ratio=increase,crop=1280:720,zoompan=z='min(zoom+0.0008,1.15)':d=1:x='iw/2-(iw/zoom/2)':y='ih/2-(ih/zoom/2)':s=1280x720:fps=25",
                    "-c:v", "libx264", "-pix_fmt", "yuv420p", clip_p
                ]
                subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                clips.append(clip_p)
            concat_txt = os.path.join(job_dir, "concat.txt")
            open(concat_txt, "w").write("\n".join(f"file '{c}'" for c in clips))
            out_mp4 = os.path.join(job_dir, "output.mp4")
            subprocess.run(["ffmpeg", "-y", "-f", "concat", "-safe", "0", "-i", concat_txt, "-c", "copy", out_mp4], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            outputs["video"] = "output.mp4"

        set_status(job_dir, "done", "Finished!", done=True, outputs=outputs)
    except Exception as e:
        set_status(job_dir, "error", str(e), done=True, error=str(e), outputs=outputs)

if __name__ == "__main__":
    main()
