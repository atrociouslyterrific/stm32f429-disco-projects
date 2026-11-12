"""Writes the practice conversations the fish talk model learns from.

Runs on GitHub (never on the board, never on your own computer). Every line is in capitals
and uses only the characters the board's font can draw.

Each line of the output is one thing an animal says, with what the model is told first:

    EVENT KIND TRAIT TRAIT FEELING NAME: what it says
    EVENT KIND TRAIT TRAIT FEELING NAME SAID what they said: its reply

EVENT    what is going on (BITE, BITTEN, FIGHT, FOUGHT, BORN, NEWBORN, LIKE, LIKED, HATE,
         HATED, EVOLVE, EVOLVED, MOURN, GLOAT, GREET, GREETED, HUNGRY, BEGGED)
KIND     what animal is speaking (FRY, SQUID, TURTLE, SHARK, RAY, ORCA, WHALE)
TRAIT    its two strongest personality traits (or EVEN)
FEELING  how it feels about who it is talking to (FRIEND, ENEMY, FAMILY, STRANGER)
NAME     who it is talking to

The same format is built by Fish/fish_talk.c when an animal actually speaks.

Usage: python3 tools/talk_corpus.py <output file> [number of conversations]
"""

import random
import sys

LINE_MAX = 34  # characters an animal may say (fits the board's log with "ADA: " in front)
ALLOWED = set("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 !?:.-+")

NAMES = ["ADA", "BOB", "CAL", "DOT", "EVE", "FIN", "GUS", "HAL", "IVY", "JAX", "KIT", "LEO", "MAX",
         "NED", "OAK", "PIP", "RAY", "SAM", "TAD", "UMA", "VIC", "WES", "XAN", "YAZ", "ZOE"]
KINDS = ["FRY", "SQUID", "TURTLE", "SHARK", "RAY", "ORCA", "WHALE"]
TRAITS = [("BOLD", "TIMID"), ("SOCIAL", "LONER"), ("ANGRY", "CALM"), ("LOYAL", "FICKLE"), ("CURIOUS", "CAUTIOUS")]
FEELINGS = ["FRIEND", "ENEMY", "FAMILY", "STRANGER"]

# Each kind of animal has its own way of talking: a sound it starts with, and things it
# says about itself.
VOICE = {
    "FRY": (["EEP!", "OH!", "WHEE!"], ["I AM SO SMALL", "TINY FINS GO", "I WILL BE BIG ONE DAY"]),
    "SQUID": (["SPLOOSH!", "BLUB!", "OOH!"], ["ALL MY ARMS AGREE", "INK READY", "EIGHT ARMS EIGHT IDEAS"]),
    "TURTLE": (["HMM.", "WELL WELL.", "SLOWLY NOW."], ["NO RUSH", "MY SHELL HAS SEEN IT ALL", "PATIENCE"]),
    "SHARK": (["GRR.", "HEH.", "SNAP!"], ["I SMELL FEAR", "TEETH FIRST", "KEEP SWIMMING"]),
    "RAY": (["SHHH.", "AH.", "MMM."], ["GLIDE WITH ME", "SO PEACEFUL", "I DRIFT"]),
    "ORCA": (["HA!", "HUH.", "OI!"], ["TOP OF THE SEA", "I RULE THESE WATERS", "BOW TO THE ORCA"]),
    "WHALE": (["HMMMM.", "OOOOH.", "BLOOOP."], ["THE SEA IS DEEP", "I SING TO THE SEA", "SO MUCH WATER"]),
}

# Things each personality trait adds to what an animal says.
FLAIR = {
    "BOLD": ["I FEAR NOTHING", "COME AT ME", "WATCH THIS"],
    "TIMID": ["UM...", "P-PLEASE", "OH DEAR"],
    "SOCIAL": ["LETS SWIM TOGETHER", "FRIENDS?", "THE MORE THE BETTER"],
    "LONER": ["LEAVE ME BE", "I SWIM ALONE", "TOO CROWDED"],
    "ANGRY": ["OR ELSE", "I WILL BITE", "DO NOT TEST ME"],
    "CALM": ["EASY NOW", "NO NEED TO FIGHT", "BREATHE"],
    "LOYAL": ["FAMILY FIRST", "I WILL NOT FORGET", "I STAND BY MY OWN"],
    "FICKLE": ["WHATEVER", "MAYBE", "I MIGHT CHANGE MY MIND"],
    "CURIOUS": ["WHAT IS THAT?", "TELL ME MORE", "I WONDER"],
    "CAUTIOUS": ["CAREFUL", "STAY BACK", "SLOWLY"],
}

# What gets said, by situation. {N} is the one being spoken to, {K} the speaker's kind.
# Lists under a trait or feeling are used when the speaker has that trait or feeling.
SAY = {
    "BITE": {
        "": ["YOU LOOK TASTY {N}", "DINNER TIME {N}", "GOT YOU {N}!", "NOTHING PERSONAL {N}", "YUM",
             "YOU ARE MY LUNCH", "TOO SLOW {N}", "MORE PLEASE", "I AM SO HUNGRY", "ONE MORE BITE"],
        "TIMID": ["SORRY SORRY {N}", "I HAD TO {N}", "FORGIVE ME"],
        "CALM": ["IT IS JUST NATURE {N}", "FORGIVE ME {N}"],
        "ANGRY": ["THAT IS FOR LAST TIME", "GET IN MY BELLY {N}"],
        "BOLD": ["NOBODY ESCAPES ME", "TOO EASY {N}"],
        "ENEMY": ["I WARNED YOU {N}", "PAYBACK {N}"],
    },
    "BITTEN": {
        "": ["OW! {N} STOP!", "HELP!", "YOU WILL PAY {N}", "LEAVE ME ALONE", "WHY {N}?", "THAT HURT!",
             "I WILL REMEMBER THIS", "SOMEONE HELP ME", "GET OFF ME {N}", "OUCH OUCH OUCH"],
        "BOLD": ["IS THAT ALL {N}?", "I HAVE HAD WORSE"],
        "TIMID": ["P-PLEASE NO", "I AM TOO SMALL TO EAT"],
        "LOYAL": ["MY FAMILY WILL HEAR OF THIS", "FAMILY HELP ME"],
        "ANGRY": ["YOU WILL REGRET THAT {N}", "I WILL BITE BACK"],
        "FRIEND": ["I THOUGHT WE WERE FRIENDS", "HOW COULD YOU {N}?"],
        "FAMILY": ["WE ARE FAMILY {N}!", "NOT YOU TOO {N}"],
    },
    "FIGHT": {
        "": ["THIS ENDS NOW {N}", "I HAVE HAD ENOUGH OF YOU", "FIGHT ME {N}", "YOU STOLE MY FOOD",
             "OUT OF MY WATER {N}", "TIME TO SETTLE THIS", "YOU ASKED FOR IT {N}"],
        "ANGRY": ["I HATE YOU {N}", "NO MORE WARNINGS"],
        "BOLD": ["I WILL NOT BACK DOWN", "BRING YOUR FRIENDS {N}"],
        "TIMID": ["I HAVE TO DO THIS {N}", "PLEASE JUST GO {N}"],
        "LOYAL": ["THIS IS FOR MY FAMILY", "YOU HURT MY FRIEND"],
    },
    "FOUGHT": {
        "": ["BRING IT {N}", "YOU STARTED THIS", "I WILL WIN", "STOP IT {N}", "NOT AGAIN {N}",
             "FINE LETS GO", "YOU WILL LOSE {N}"],
        "CALM": ["WE CAN STOP THIS {N}", "ENOUGH {N}"],
        "TIMID": ["I DID NOTHING!", "LEAVE ME ALONE {N}"],
        "BOLD": ["IS THAT YOUR BEST?", "I EAT FIGHTS FOR FUN"],
    },
    "BORN": {
        "": ["WELCOME LITTLE {N}", "HELLO {N} MY BABY", "STAY CLOSE {N}", "YOU HAVE MY FINS {N}",
             "GROW BIG {N}", "EAT WELL {N}", "WELCOME TO THE SEA", "LOOK AT YOU {N}"],
        "LOYAL": ["YOU ARE FAMILY NOW {N}", "I WILL PROTECT YOU"],
        "CAUTIOUS": ["WATCH OUT FOR SHARKS {N}", "STAY NEAR THE PLANTS"],
        "BOLD": ["YOU WILL BE A HUNTER {N}", "BE BRAVE {N}"],
        "CURIOUS": ["WHAT WILL YOU BECOME {N}?"],
    },
    "NEWBORN": {
        "": ["HI {N}", "HELLO {N}", "WHERE AM I?", "I AM HUNGRY", "IS THAT FOOD?", "I WILL GROW BIG",
             "WHAT IS WATER?", "HI MUM", "HI DAD", "EVERYTHING IS SO BIG"],
        "CURIOUS": ["WHAT IS THAT {N}?", "TELL ME EVERYTHING"],
        "TIMID": ["IT IS SCARY OUT HERE", "DO NOT LEAVE ME {N}"],
        "BOLD": ["I AM READY FOR ANYTHING", "WATCH ME SWIM {N}"],
    },
    "LIKE": {
        "": ["YOU ARE ALRIGHT {N}", "SWIM WITH ME {N}", "I LIKE YOU {N}", "FRIENDS {N}?", "WE MAKE A GOOD TEAM",
             "GOOD TO SEE YOU {N}", "LETS STICK TOGETHER"],
        "SOCIAL": ["BEST FRIENDS {N}!", "YOU ARE MY FAVOURITE"],
        "LONER": ["YOU ARE NOT SO BAD {N}", "I GUESS YOU CAN STAY"],
        "FAMILY": ["FAMILY IS EVERYTHING {N}"],
    },
    "LIKED": {
        "": ["ME TOO {N}", "FRIENDS FOREVER", "OF COURSE {N}", "LETS FIND FOOD", "I KNEW IT", "AWW {N}",
             "TOGETHER THEN"],
        "LONER": ["DONT GET USED TO IT", "IF YOU MUST {N}"],
        "FICKLE": ["FOR NOW {N}", "WE WILL SEE"],
        "SOCIAL": ["YAY! FRIENDS!", "I LOVE THIS {N}"],
    },
    "HATE": {
        "": ["I CANNOT STAND YOU {N}", "STAY AWAY {N}", "YOU ARE TROUBLE {N}", "I SEE YOU {N}",
             "WATCH YOUR TAIL {N}", "KEEP YOUR DISTANCE", "YOU AGAIN {N}?"],
        "ANGRY": ["ONE DAY {N}. ONE DAY.", "I WILL GET YOU {N}"],
        "TIMID": ["PLEASE GO AWAY {N}", "YOU SCARE ME {N}"],
        "LOYAL": ["YOU HURT MY FAMILY {N}", "I NEVER FORGET {N}"],
    },
    "HATED": {
        "": ["THE FEELING IS MUTUAL", "WHATEVER {N}", "YOU WILL REGRET THAT", "FINE BY ME", "WHAT DID I DO?",
             "GOOD {N}", "SAME TO YOU {N}"],
        "CALM": ["I WISH YOU PEACE {N}", "LET IT GO {N}"],
        "ANGRY": ["TRY ME {N}", "BIG WORDS {N}"],
        "TIMID": ["OH NO", "I AM SORRY {N}"],
    },
    "EVOLVE": {
        "": ["LOOK {N} I AM A {K} NOW", "I GREW INTO A {K}!", "BEHOLD A {K}", "I FEEL BIGGER {N}", "{K} POWER!",
             "CALL ME {K} NOW", "I AM A {K}!"],
        "BOLD": ["FEAR THE {K}", "NOBODY CAN STOP A {K}"],
        "TIMID": ["I AM A {K}? WOW", "UM I CHANGED {N}"],
        "CURIOUS": ["WHAT CAN A {K} DO?", "SO THIS IS A {K}"],
    },
    "EVOLVED": {
        "": ["WOW {N}", "YOU GOT SO BIG", "TEACH ME {N}", "SHOW OFF", "CONGRATS {N}", "PLEASE DO NOT EAT ME",
             "LOOKING GOOD {N}"],
        "ENEMY": ["STILL UGLY {N}", "BIGGER TARGET {N}"],
        "FRIEND": ["SO PROUD OF YOU {N}", "AMAZING {N}!"],
        "FAMILY": ["THE FAMILY GROWS STRONG", "WELL DONE {N}"],
    },
    "MOURN": {
        "": ["YOU ATE MY FRIEND {N}", "MONSTER!", "I WILL NOT FORGET THIS", "WHY DID YOU DO IT {N}?",
             "THEY WERE MY FAMILY {N}", "HOW COULD YOU {N}?", "YOU WILL PAY FOR THAT"],
        "TIMID": ["AM I NEXT?", "PLEASE NOT ME TOO"],
        "LOYAL": ["I WILL AVENGE THEM {N}", "MY FAMILY REMEMBERS"],
        "CALM": ["SUCH IS THE SEA", "REST WELL MY FRIEND"],
    },
    "GLOAT": {
        "": ["THAT IS THE SEA", "I WAS HUNGRY", "YOU ARE NEXT {N}", "SORRY {N}", "SUCH IS LIFE",
             "TASTY TOO", "STAY OUT OF MY WAY {N}"],
        "TIMID": ["I DID NOT MEAN IT", "I AM SO SORRY {N}"],
        "CALM": ["IT HAD TO BE", "I TAKE NO JOY IN IT"],
        "BOLD": ["WANT TO BE NEXT {N}?", "THE STRONG EAT"],
    },
    "GREET": {
        "": ["HI {N}", "NICE WATER TODAY {N}", "SEEN ANY FOOD {N}?", "HELLO {N}", "WHERE ARE YOU GOING?",
             "HOW IS THE FAMILY {N}?", "LOVELY DAY {N}", "WHAT ARE YOU UP TO?"],
        "ENEMY": ["WHAT DO YOU WANT {N}?", "YOU AGAIN"],
        "FRIEND": ["THERE YOU ARE {N}!", "MISSED YOU {N}"],
        "FAMILY": ["HELLO FAMILY", "HOW ARE YOU {N}?"],
        "CURIOUS": ["WHAT IS NEW {N}?", "SEEN ANYTHING STRANGE?"],
        "LONER": ["OH. IT IS YOU {N}"],
    },
    "GREETED": {
        "": ["HI {N}", "NOT MUCH FOOD AROUND", "JUST SWIMMING", "HELLO {N}", "SAME AS ALWAYS", "ALL GOOD {N}"],
        "ENEMY": ["GO AWAY {N}", "NOT YOU AGAIN"],
        "FRIEND": ["HELLO FRIEND", "GOOD TO SEE YOU {N}"],
        "LONER": ["HMPH", "I WAS LEAVING"],
    },
    "HUNGRY": {
        "": ["I AM STARVING {N}", "ANY FOOD NEAR {N}?", "SO HUNGRY", "SHARE YOUR FOOD {N}", "MY BELLY HURTS",
             "WHERE IS ALL THE FOOD?"],
        "TIMID": ["CAN I HAVE SOME {N}?", "UM I AM HUNGRY"],
        "ANGRY": ["GIVE ME FOOD {N}", "SOMEONE FEED ME"],
    },
    "BEGGED": {
        "": ["TRY THE PLANTS", "FIND YOUR OWN", "FOLLOW ME {N}", "ME TOO {N}", "STAY AWAY FROM MY FOOD",
             "THE TOP IS GOOD", "WAIT FOR THE PLANTS"],
        "SOCIAL": ["LETS LOOK TOGETHER {N}", "SHARE WITH ME {N}"],
        "LONER": ["NOT MY PROBLEM", "GO AWAY {N}"],
    },
}

# Answers when the other one asked a question.
ANSWERS = {
    "GREETED": ["NO FOOD HERE", "BY THE PLANTS {N}", "NOT TELLING", "JUST AROUND", "THEY ARE FINE {N}",
                "NOWHERE MUCH", "ASK SOMEONE ELSE"],
    "BEGGED": ["NEAR THE PLANTS {N}", "NONE LEFT", "TRY UP TOP {N}", "NO", "MAYBE LATER {N}"],
    "BITTEN": ["BECAUSE I AM SMALL?", "STOP ASKING AND STOP!"],
    "GLOAT": ["BECAUSE I WAS HUNGRY", "THE SEA DECIDES {N}", "WHY NOT?"],
    "EVOLVED": ["ALL SORTS OF THINGS", "BITE HARDER I BET"],
    "NEWBORN": ["I AM {N}S BABY", "I HAVE NO IDEA"],
}

# Who answers each kind of opening line.
REPLY = {"BITE": "BITTEN", "FIGHT": "FOUGHT", "BORN": "NEWBORN", "LIKE": "LIKED", "HATE": "HATED",
         "EVOLVE": "EVOLVED", "MOURN": "GLOAT", "GREET": "GREETED", "HUNGRY": "BEGGED"}
# How the one answering usually feels about the one who spoke first.
REPLY_FEELING = {"BITE": ["ENEMY", "STRANGER", "FRIEND", "FAMILY"], "FIGHT": ["ENEMY"], "BORN": ["FAMILY"],
                 "LIKE": ["FRIEND", "STRANGER"], "HATE": ["ENEMY", "STRANGER"], "MOURN": ["ENEMY", "STRANGER"]}
OPEN_FEELING = {"BITE": ["STRANGER", "ENEMY"], "FIGHT": ["ENEMY"], "BORN": ["FAMILY"],
                "LIKE": ["FRIEND", "FAMILY"], "HATE": ["ENEMY"], "MOURN": ["ENEMY"]}


def traits(rng):
    """Two trait words, or EVEN for a trait that isn't strong."""
    picked = rng.sample(TRAITS, 2)
    return [pair[rng.randint(0, 1)] if rng.random() < 0.8 else "EVEN" for pair in picked]


def fill(text, name, kind):
    return text.replace("{N}", name).replace("{K}", kind)


def line(rng, event, kind, tr, feeling, name, said=None):
    """What an animal says, built from its situation, species, traits and feelings."""
    for _ in range(40):
        table = SAY[event]
        choices = list(table[""])
        for key in tr + [feeling]:
            choices += table.get(key, []) * 2  # its traits and feelings come up more often
        if said and said.endswith("?") and event in ANSWERS and rng.random() < 0.75:
            choices = ANSWERS[event]
        parts = [fill(rng.choice(choices), name, kind)]
        sound, about_me = VOICE[kind]
        if rng.random() < 0.3:
            parts.insert(0, rng.choice(sound))
        if rng.random() < 0.15:
            parts.append(rng.choice(about_me))
        flair = [t for t in tr if t in FLAIR]
        if flair and rng.random() < 0.3:
            parts.append(rng.choice(FLAIR[rng.choice(flair)]))
        text = " ".join(parts)
        if not text[-1] in "!?." and rng.random() < 0.2:
            text += "!"
        if len(text) <= LINE_MAX:
            return text
    return fill(table[""][0], name, kind)[:LINE_MAX]


def prompt(event, kind, tr, feeling, name, said=None):
    head = " ".join([event, kind, tr[0], tr[1], feeling, name])
    return head + (" SAID " + said if said else "") + ": "


def kind_for(event, rng):
    if event == "BORN":
        return rng.choice(KINDS[1:])  # parents have grown up a bit
    if event == "NEWBORN":
        return "FRY"
    return rng.choice(KINDS)


def conversation(rng):
    opener = rng.choice(list(REPLY))
    a, b = rng.sample(NAMES, 2)  # a speaks first, to b
    kind_a, kind_b = kind_for(opener, rng), kind_for(REPLY[opener], rng)
    tr_a, tr_b = traits(rng), traits(rng)
    feel_a = rng.choice(OPEN_FEELING.get(opener, FEELINGS))
    feel_b = rng.choice(REPLY_FEELING.get(opener, FEELINGS))
    first = line(rng, opener, kind_a, tr_a, feel_a, b)
    second = line(rng, REPLY[opener], kind_b, tr_b, feel_b, a, said=first)
    return [prompt(opener, kind_a, tr_a, feel_a, b) + first,
            prompt(REPLY[opener], kind_b, tr_b, feel_b, a, said=first) + second]


def main():
    out = sys.argv[1]
    count = int(sys.argv[2]) if len(sys.argv) > 2 else 60000
    rng = random.Random(20261112)
    lines = []
    for _ in range(count):
        lines += conversation(rng)
    for text in lines:
        assert set(text) <= ALLOWED, text
    with open(out, "w") as f:
        f.write("\n".join(lines) + "\n")
    print(f"wrote {len(lines)} lines to {out}")
    for text in lines[:12]:
        print("  " + text)


if __name__ == "__main__":
    main()
