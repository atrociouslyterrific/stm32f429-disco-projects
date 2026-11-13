"""Trains the Writer's language model on GitHub's computers (never on the board or your Mac).

The model is a small transformer that reads and writes one character at a time, the same
basic design as large language models, made small enough to run on the STM32 board. It
learns from the books gathered by tools/writer_corpus.py, and each training run carries on
from the last (given its saved writer_model.pt).

It is saved as lm_model.h: every number rounded to a whole number from -127 to 127 (with a
scale for each row), the way the board stores it.

Usage: python3 tools/train_writer.py <corpus.txt> <output folder> [previous writer_model.pt] [previous training json]
  TRAIN_MINUTES  how long to train for (default 60)
"""

import json
import math
import os
import random
import sys
import time

import numpy as np
import torch
import torch.nn as nn
import torch.nn.functional as F

VOCAB = "\n !\"'(),-./0123456789:;?ABCDEFGHIJKLMNOPQRSTUVWXYZ"
D, LAYERS, HEADS, CTX = 128, 4, 4, 128
FF = 4 * D
FORMAT = 1  # bump when the layout of lm_model.h changes
SAMPLE_PROMPTS = ["ONCE UPON A TIME", "THE KING SAID TO HIS DAUGHTER", "A FOX SAW SOME GRAPES", "THE LITTLE MERMAID"]

torch.manual_seed(1)
random.seed(1)
torch.set_num_threads(os.cpu_count() or 4)


class Block(nn.Module):
    def __init__(self):
        super().__init__()
        self.ln1 = nn.LayerNorm(D)
        self.qkv = nn.Linear(D, 3 * D, bias=False)
        self.proj = nn.Linear(D, D, bias=False)
        self.ln2 = nn.LayerNorm(D)
        self.fc = nn.Linear(D, FF, bias=False)
        self.out = nn.Linear(FF, D, bias=False)

    def forward(self, x):
        B, T, _ = x.shape
        q, k, v = self.qkv(self.ln1(x)).split(D, dim=2)
        q, k, v = (t.view(B, T, HEADS, D // HEADS).transpose(1, 2) for t in (q, k, v))
        a = F.scaled_dot_product_attention(q, k, v, is_causal=True)
        x = x + self.proj(a.transpose(1, 2).reshape(B, T, D))
        return x + self.out(F.gelu(self.fc(self.ln2(x)), approximate="tanh"))


class Model(nn.Module):
    def __init__(self):
        super().__init__()
        self.tok = nn.Embedding(len(VOCAB), D)
        self.pos = nn.Embedding(CTX, D)
        self.blocks = nn.ModuleList(Block() for _ in range(LAYERS))
        self.lnf = nn.LayerNorm(D)
        for m in self.modules():
            if isinstance(m, (nn.Linear, nn.Embedding)):
                nn.init.normal_(m.weight, std=0.02)

    def forward(self, idx):
        x = self.tok(idx) + self.pos(torch.arange(idx.shape[1]))
        for b in self.blocks:
            x = b(x)
        return self.lnf(x) @ self.tok.weight.T


def encode(text):
    return [VOCAB.index(c) for c in text if c in VOCAB]


@torch.no_grad()
def write(model, prompt, length=160, temperature=0.75):
    ids = encode(prompt)
    for _ in range(length):
        logits = model(torch.tensor([ids[-CTX:]]))[0, -1] / temperature
        ids.append(torch.multinomial(F.softmax(logits, -1), 1).item())
    return "".join(VOCAB[i] for i in ids)[len(prompt):]


def quantise(w):
    w = w.detach().numpy().astype(np.float64)
    scale = np.abs(w).max(axis=1) / 127.0
    scale[scale == 0] = 1.0
    return np.clip(np.round(w / scale[:, None]), -127, 127).astype(np.int8), scale.astype(np.float32)


def c_array(ctype, name, values, per_line=24):
    vals = [str(int(v)) if ctype == "int8_t" else repr(float(v)) + "f" for v in np.asarray(values).ravel()]
    rows = [", ".join(vals[i:i + per_line]) for i in range(0, len(vals), per_line)]
    return f"static const {ctype} {name}[{len(vals)}] = {{\n  " + ",\n  ".join(rows) + "\n};\n"


def export(model, path):
    rounded, parts = {}, []

    def matrix(name, w):
        q, s = quantise(w)
        rounded[name] = q.astype(np.float32) * s[:, None]
        return q, s

    tok_q, tok_s = matrix("tok", model.tok.weight)
    pos_q, pos_s = matrix("pos", model.pos.weight)
    stacks = {k: [] for k in ["qkv", "qkv_s", "proj", "proj_s", "fc", "fc_s", "out", "out_s", "ln1_g", "ln1_b", "ln2_g", "ln2_b"]}
    for i, b in enumerate(model.blocks):
        for key, layer in [("qkv", b.qkv), ("proj", b.proj), ("fc", b.fc), ("out", b.out)]:
            q, s = matrix(f"{key}{i}", layer.weight)
            stacks[key].append(q)
            stacks[key + "_s"].append(s)
        stacks["ln1_g"].append(b.ln1.weight.detach().numpy()); stacks["ln1_b"].append(b.ln1.bias.detach().numpy())
        stacks["ln2_g"].append(b.ln2.weight.detach().numpy()); stacks["ln2_b"].append(b.ln2.bias.detach().numpy())
    vocab_c = VOCAB.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n")
    parts.append(f"""// Made by tools/train_writer.py on GitHub. Don't edit: replaced by every training run.
// The Writer's language model: a {LAYERS}-layer transformer reading one character at a time.
#define LM_FORMAT {FORMAT}
#define LM_V {len(VOCAB)}
#define LM_D {D}
#define LM_L {LAYERS}
#define LM_H {HEADS}
#define LM_CTX {CTX}
#define LM_FF {FF}
#define LM_VOCAB "{vocab_c}"
""")
    parts.append(c_array("int8_t", "LM_TOK", tok_q) + c_array("float", "LM_TOK_S", tok_s))
    parts.append(c_array("int8_t", "LM_POS", pos_q) + c_array("float", "LM_POS_S", pos_s))
    for key in ["qkv", "proj", "fc", "out"]:
        parts.append(c_array("int8_t", "LM_" + key.upper(), np.stack(stacks[key])))
        parts.append(c_array("float", "LM_" + key.upper() + "_S", np.stack(stacks[key + "_s"])))
    for key in ["ln1_g", "ln1_b", "ln2_g", "ln2_b"]:
        parts.append(c_array("float", "LM_" + key.upper(), np.stack(stacks[key])))
    parts.append(c_array("float", "LM_LNF_G", model.lnf.weight.detach().numpy()))
    parts.append(c_array("float", "LM_LNF_B", model.lnf.bias.detach().numpy()))
    with open(path, "w") as f:
        f.write("".join(parts))
    return rounded


@torch.no_grad()
def reference(model, rounded, prompt, path):
    """The rounded model's scores for the next character, for the C code to match."""
    m = Model()
    m.load_state_dict(model.state_dict())
    m.tok.weight.copy_(torch.tensor(rounded["tok"]))
    m.pos.weight.copy_(torch.tensor(rounded["pos"]))
    for i, b in enumerate(m.blocks):
        for key, layer in [("qkv", b.qkv), ("proj", b.proj), ("fc", b.fc), ("out", b.out)]:
            layer.weight.copy_(torch.tensor(rounded[f"{key}{i}"]))
    logits = m(torch.tensor([encode(prompt)]))[0, -1].numpy()
    with open(path, "w") as f:
        f.write(prompt + "\n" + " ".join(f"{v:.6f}" for v in logits) + "\n")


def main():
    corpus_path, out = sys.argv[1], sys.argv[2]
    resume = sys.argv[3] if len(sys.argv) > 3 else None
    resume_summary = sys.argv[4] if len(sys.argv) > 4 else None
    os.makedirs(out, exist_ok=True)
    budget = float(os.environ.get("TRAIN_MINUTES", "60")) * 60
    text = open(corpus_path).read()
    data = torch.tensor(encode(text), dtype=torch.long)
    cut = int(len(data) * 0.95)
    train_data, check_data = data[:cut], data[cut:]
    corpus_words, corpus_lines = len(text.split()), text.count("\n")

    model = Model()
    params = sum(p.numel() for p in model.parameters())
    prior_steps, prior_minutes, peak = 0, 0.0, 2e-3
    if resume and os.path.exists(resume):
        try:
            saved = torch.load(resume, map_location="cpu", weights_only=False)
            model.load_state_dict(saved["model"])
            prior_steps, prior_minutes, peak = saved.get("steps", 0), saved.get("minutes", 0.0), 8e-4
            print(f"carrying on from {resume}: {prior_steps:,} steps, {prior_minutes:.0f} minutes so far")
        except Exception as e:
            print(f"couldn't carry on from {resume} ({e}); starting from scratch")
    print(f"model: {params:,} numbers ({LAYERS} layers, {D} wide, {HEADS} heads, {CTX} characters of memory)")
    print(f"learning from {len(text):,} characters, {corpus_words:,} words, {corpus_lines:,} paragraphs")

    batch = 32
    opt = torch.optim.AdamW(model.parameters(), lr=peak, weight_decay=0.05)
    space = VOCAB.index(" ")

    def windows(src):
        starts = torch.randint(0, len(src) - CTX - 1, (batch,))
        return torch.stack([src[s:s + CTX] for s in starts]), torch.stack([src[s + 1:s + CTX + 1] for s in starts])

    start, step, history, timeline = time.time(), 0, [], []
    chars_seen = words_seen = 0
    last_report, last_snap = 0.0, -1e9
    progress_path = os.path.join(out, "writer_progress.json")

    def summary(state, samples):
        minutes = round((time.time() - start) / 60, 1)
        return {"state": state, "step": step, "minutes": minutes, "budget_minutes": round(budget / 60, 1),
                "total_steps": prior_steps + step, "total_minutes": round(prior_minutes + minutes, 1),
                "params": params, "layers": LAYERS, "width": D, "heads": HEADS, "memory": CTX, "batch": batch,
                "chars_seen": chars_seen, "words_seen": words_seen, "corpus_lines": corpus_lines,
                "corpus_words": corpus_words, "corpus_chars": len(text),
                "history": history[::max(1, len(history) // 300)], "samples": samples, "timeline": timeline,
                "updated": int(time.time())}

    def report(state, samples):
        tmp = progress_path + ".tmp"
        with open(tmp, "w") as f:
            json.dump(summary(state, samples), f)
        os.replace(tmp, progress_path)

    while time.time() - start < budget:
        elapsed = time.time() - start
        lr = peak * (0.1 + 0.9 * 0.5 * (1 + math.cos(math.pi * min(1.0, elapsed / budget))))
        for g in opt.param_groups:
            g["lr"] = lr
        x, y = windows(train_data)
        loss = F.cross_entropy(model(x).view(-1, len(VOCAB)), y.view(-1))
        opt.zero_grad()
        loss.backward()
        torch.nn.utils.clip_grad_norm_(model.parameters(), 1.0)
        opt.step()
        step += 1
        chars_seen += x.numel()
        words_seen += int((x == space).sum())
        if step % 50 == 0:
            model.eval()
            with torch.no_grad():
                cx, cy = windows(check_data)
                val = F.cross_entropy(model(cx).view(-1, len(VOCAB)), cy.view(-1)).item()
            model.train()
            history.append([prior_steps + step, round(loss.item(), 4), round(val, 4)])
            if time.time() - last_report > 25:
                model.eval()
                samples = [[p, write(model, p, 100)] for p in SAMPLE_PROMPTS[:2]]
                model.train()
                if time.time() - last_snap > max(90, budget / 8):
                    timeline.append({"minutes": round((time.time() - start) / 60, 1), "step": prior_steps + step, "samples": samples})
                    last_snap = time.time()
                report("training", samples)
                last_report = time.time()
                print(f"step {step}: loss {loss.item():.3f}, held-out {val:.3f}, {elapsed / 60:.1f} min")

    model.eval()
    samples = [[p, write(model, p, 160)] for p in SAMPLE_PROMPTS]
    timeline.append({"minutes": round((time.time() - start) / 60, 1), "step": prior_steps + step, "samples": samples[:2]})
    report("done", samples)
    for p, s in samples:
        print(f"  {p}{s}")
    rounded = export(model, os.path.join(out, "lm_model.h"))
    reference(model, rounded, SAMPLE_PROMPTS[1], os.path.join(out, "lm_reference.txt"))
    torch.save({"model": model.state_dict(), "steps": prior_steps + step,
                "minutes": prior_minutes + (time.time() - start) / 60}, os.path.join(out, "writer_model.pt"))
    with open(os.path.join(out, "writer_training.json"), "w") as f:
        json.dump(summary("done", samples), f)
    print(f"trained {step} steps ({prior_steps + step} in all); saved {out}/lm_model.h")


if __name__ == "__main__":
    main()
