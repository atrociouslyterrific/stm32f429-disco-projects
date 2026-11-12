"""Trains the fish talk model: a small language model, built from scratch, that writes what
the animals say. Runs on GitHub's computers (never on the board, never on your own computer).

The model reads and writes one character at a time. It is a transformer, the same basic
design as large language models, just tiny: a few layers of "attention" (each character
looking back at the earlier ones) and small neural networks in between. It is small enough
to run on the STM32 board itself.

Afterwards the model is saved as talk_model.h: its numbers rounded to whole numbers from
-127 to 127 (int8) with a scale for each row, which is how the board stores it.

Usage: python3 tools/train_talk.py <corpus.txt> <output folder>
  TRAIN_MINUTES  how long to train for (default 30)
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

# The model's size. Fish/fish_talk.c reads these from talk_model.h.
VOCAB = "\n !?:.-+0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"
D, LAYERS, HEADS, CTX = 80, 3, 4, 128
FF = 4 * D
FORMAT = 1  # bump when the layout of talk_model.h changes

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


class TalkModel(nn.Module):
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
        return self.lnf(x) @ self.tok.weight.T  # the output reuses the input's character table


def encode(text):
    return [VOCAB.index(c) for c in text]


def batches(lines, batch):
    """Windows of the text that each start at the beginning of a line, as the board will."""
    starts, text, pos = [], [], 0
    for line in lines:
        starts.append(pos)
        ids = encode(line + "\n")
        text += ids
        pos += len(ids)
    data = torch.tensor(text, dtype=torch.long)
    starts = [s for s in starts if s + CTX + 1 <= len(data)]
    while True:
        pick = random.sample(starts, batch)
        x = torch.stack([data[s:s + CTX] for s in pick])
        y = torch.stack([data[s + 1:s + CTX + 1] for s in pick])
        yield x, y


@torch.no_grad()
def say(model, prompt, temperature=0.7):
    ids = encode(prompt)
    for _ in range(40):
        logits = model(torch.tensor([ids[-CTX:]]))[0, -1] / temperature
        nxt = torch.multinomial(F.softmax(logits, -1), 1).item()
        if VOCAB[nxt] == "\n":
            break
        ids.append(nxt)
    return "".join(VOCAB[i] for i in ids)[len(prompt):]


SAMPLE_PROMPTS = [
    "BITE SHARK BOLD ANGRY STRANGER ADA: ",
    "BITTEN FRY TIMID LOYAL ENEMY GUS SAID YOU LOOK TASTY ADA: ",
    "BORN WHALE LOYAL CALM FAMILY PIP: ",
    "NEWBORN FRY CURIOUS EVEN FAMILY ZOE SAID WELCOME LITTLE PIP: ",
    "GREET TURTLE SOCIAL CURIOUS FRIEND MAX: ",
    "EVOLVE ORCA BOLD LONER FRIEND IVY: ",
    "HATE SQUID ANGRY FICKLE ENEMY BOB: ",
    "MOURN RAY LOYAL CALM ENEMY JAX: ",
]


def quantise(w):
    """Each row as whole numbers -127..127 and one scale (what the board stores)."""
    w = w.detach().numpy().astype(np.float64)
    scale = np.abs(w).max(axis=1) / 127.0
    scale[scale == 0] = 1.0
    q = np.clip(np.round(w / scale[:, None]), -127, 127).astype(np.int8)
    return q, scale.astype(np.float32)


def c_array(ctype, name, values, per_line=24):
    vals = [str(int(v)) if ctype == "int8_t" else repr(float(v)) + "f" for v in np.asarray(values).ravel()]
    rows = [", ".join(vals[i:i + per_line]) for i in range(0, len(vals), per_line)]
    return f"static const {ctype} {name}[{len(vals)}] = {{\n  " + ",\n  ".join(rows) + "\n};\n"


def export(model, path):
    """Write talk_model.h, and return the rounded model for checking against the C code."""
    parts, rounded = [], {}

    def matrix(name, w):
        q, s = quantise(w)
        rounded[name] = q.astype(np.float32) * s[:, None]
        return q, s

    tok_q, tok_s = matrix("tok", model.tok.weight)
    pos_q, pos_s = matrix("pos", model.pos.weight)
    stacks = {k: [] for k in ["ln1_g", "ln1_b", "qkv", "qkv_s", "proj", "proj_s", "ln2_g", "ln2_b", "fc", "fc_s", "out", "out_s"]}
    for i, b in enumerate(model.blocks):
        for key, layer in [("qkv", b.qkv), ("proj", b.proj), ("fc", b.fc), ("out", b.out)]:
            q, s = matrix(f"{key}{i}", layer.weight)
            stacks[key].append(q)
            stacks[key + "_s"].append(s)
        stacks["ln1_g"].append(b.ln1.weight.detach().numpy()); stacks["ln1_b"].append(b.ln1.bias.detach().numpy())
        stacks["ln2_g"].append(b.ln2.weight.detach().numpy()); stacks["ln2_b"].append(b.ln2.bias.detach().numpy())

    head = f"""// Made by tools/train_talk.py on GitHub. Don't edit: replaced by every training run.
// The fish talk model: a {LAYERS}-layer transformer reading one character at a time.
#define TALK_FORMAT {FORMAT}
#define TALK_V {len(VOCAB)}
#define TALK_D {D}
#define TALK_L {LAYERS}
#define TALK_H {HEADS}
#define TALK_CTX {CTX}
#define TALK_FF {FF}
#define TALK_VOCAB "{VOCAB.encode('unicode_escape').decode()}"
"""
    parts.append(head)
    parts.append(c_array("int8_t", "TALK_TOK", tok_q) + c_array("float", "TALK_TOK_S", tok_s))
    parts.append(c_array("int8_t", "TALK_POS", pos_q) + c_array("float", "TALK_POS_S", pos_s))
    for key in ["qkv", "proj", "fc", "out"]:
        parts.append(c_array("int8_t", "TALK_" + key.upper(), np.stack(stacks[key])))
        parts.append(c_array("float", "TALK_" + key.upper() + "_S", np.stack(stacks[key + "_s"])))
    for key in ["ln1_g", "ln1_b", "ln2_g", "ln2_b"]:
        parts.append(c_array("float", "TALK_" + key.upper(), np.stack(stacks[key])))
    parts.append(c_array("float", "TALK_LNF_G", model.lnf.weight.detach().numpy()))
    parts.append(c_array("float", "TALK_LNF_B", model.lnf.bias.detach().numpy()))
    with open(path, "w") as f:
        f.write("".join(parts))
    return rounded


@torch.no_grad()
def reference(model, rounded, prompt, path):
    """The rounded model's scores for the next character after `prompt`, for the C code to match."""
    m = TalkModel()
    m.load_state_dict(model.state_dict())
    m.tok.weight.copy_(torch.tensor(rounded["tok"]))
    m.pos.weight.copy_(torch.tensor(rounded["pos"]))
    for i, b in enumerate(m.blocks):
        for key, layer in [("qkv", b.qkv), ("proj", b.proj), ("fc", b.fc), ("out", b.out)]:
            layer.weight.copy_(torch.tensor(rounded[f"{key}{i}"]))
    logits = m(torch.tensor([encode(prompt)]))[0, -1].numpy()
    with open(path, "w") as f:
        f.write(prompt.replace("\n", "") + "\n" + " ".join(f"{v:.6f}" for v in logits) + "\n")


def main():
    corpus, out = sys.argv[1], sys.argv[2]
    os.makedirs(out, exist_ok=True)
    budget = float(os.environ.get("TRAIN_MINUTES", "30")) * 60
    lines = open(corpus).read().strip().split("\n")
    random.shuffle(lines)
    cut = len(lines) // 20
    held_out, train = lines[:cut], lines[cut:]

    model = TalkModel()
    params = sum(p.numel() for p in model.parameters())
    print(f"model: {params:,} numbers ({LAYERS} layers, {D} wide, {HEADS} heads, {CTX} characters of memory)")
    opt = torch.optim.AdamW(model.parameters(), lr=3e-3, weight_decay=0.01)
    batch = 48
    data, check = batches(train, batch), batches(held_out, batch)

    start, step, history, last_report = time.time(), 0, [], 0.0
    progress_path = os.path.join(out, "talk_progress.json")

    def report(state, samples):
        tmp = progress_path + ".tmp"
        with open(tmp, "w") as f:
            json.dump({"state": state, "step": step, "minutes": round((time.time() - start) / 60, 1),
                       "budget_minutes": round(budget / 60, 1), "params": params, "history": history,
                       "samples": samples, "updated": int(time.time())}, f)
        os.replace(tmp, progress_path)

    while True:
        elapsed = time.time() - start
        if elapsed > budget:
            break
        lr = 3e-3 * (0.1 + 0.9 * 0.5 * (1 + math.cos(math.pi * min(1.0, elapsed / budget))))
        for g in opt.param_groups:
            g["lr"] = lr
        x, y = next(data)
        loss = F.cross_entropy(model(x).view(-1, len(VOCAB)), y.view(-1))
        opt.zero_grad()
        loss.backward()
        torch.nn.utils.clip_grad_norm_(model.parameters(), 1.0)
        opt.step()
        step += 1
        if step % 50 == 0:
            model.eval()
            with torch.no_grad():
                x, y = next(check)
                val = F.cross_entropy(model(x).view(-1, len(VOCAB)), y.view(-1)).item()
            model.train()
            history.append([step, round(loss.item(), 4), round(val, 4)])
            if time.time() - last_report > 20:
                model.eval()
                samples = [[p, say(model, p)] for p in SAMPLE_PROMPTS[:4]]
                model.train()
                report("training", samples)
                last_report = time.time()
                print(f"step {step}: loss {loss.item():.3f}, held-out {val:.3f}, {elapsed / 60:.1f} min")

    model.eval()
    samples = [[p, say(model, p)] for p in SAMPLE_PROMPTS]
    report("done", samples)
    for p, s in samples:
        print(f"  {p}{s}")
    rounded = export(model, os.path.join(out, "talk_model.h"))
    reference(model, rounded, SAMPLE_PROMPTS[1], os.path.join(out, "talk_reference.txt"))
    final = history[-1] if history else [step, None, None]
    with open(os.path.join(out, "talk_training.json"), "w") as f:
        json.dump({"state": "done", "step": step, "minutes": round((time.time() - start) / 60, 1), "params": params,
                   "layers": LAYERS, "width": D, "heads": HEADS, "memory": CTX, "loss": final[1], "held_out_loss": final[2],
                   "history": history[::max(1, len(history) // 200)], "samples": samples, "corpus_lines": len(lines)}, f)
    print(f"trained {step} steps; saved {out}/talk_model.h")


if __name__ == "__main__":
    main()
