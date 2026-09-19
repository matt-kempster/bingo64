from pathlib import Path

import argparse


def get_mario_pos(line):
    if "/*pos*/" not in line:
        raise Exception(f"couldn't find pos in line: {line}")
    pos_with_prefix_and_paren = line[line.index("/*pos*/") :]
    pos_with_prefix = pos_with_prefix_and_paren.strip().rstrip("),")
    return pos_with_prefix


def green_demon_object(mario_pos):
    first, second, third = mario_pos.split(", ")
    spawn_pos = ", ".join([first, str(int(second) + 1000), third])
    return (
        "    OBJECT("
        "/*model*/ MODEL_1UP, "
        f"{spawn_pos}, "
        "/*angle*/ 0, 0, 0, "
        "/*behParam*/ 0x00000000, "
        "/*beh*/ bhv1upGreenDemon),"
    )


def already_done(lines, marker) -> bool:
    for line in lines:
        if marker in line:
            return True
    return False


def get_first_return_line(lines):
    inside_script_func_local = False
    for line in lines:
        if "script_func_local_1[]" in line:
            inside_script_func_local = True
        elif inside_script_func_local and "RETURN()" in line:
            return line
    return None


def ensure_demon(script: Path) -> None:
    lines = script.read_text().splitlines()

    if already_done(lines, "bhv1upGreenDemon"):
        print(f"{script} already has a green demon")
        return

    mario_pos = None
    for line in lines:
        if "MARIO_POS(" in line:
            mario_pos = get_mario_pos(line)
            break
    if not mario_pos:
        print(f"could not find mario_pos in {script}")
        return

    the_return_line = get_first_return_line(lines)
    if not the_return_line:
        print(f"could not find return line in {script}")
        return

    response = input(f"should I do {script}?: ")
    if response.lower() != "y":
        return

    object = green_demon_object(mario_pos)

    response = input(f"does this look right:\n{object}\n?: ")
    if response.lower() != "y":
        print("bailing - go figure this out")
        exit(1)

    lines.insert(lines.index(the_return_line), object)
    script.write_text("\n".join(lines) + "\n")


def rando_star(i: int) -> str:
    return (
        "    OBJECT_WITH_ACTS(/*model*/ MODEL_STAR, "
        "                 "
        "/*pos*/     0,    0,     0, "
        "/*angle*/ 0, 0, 0,    "
        f"/*behParam*/ 0x0{i}000000, "
        "/*beh*/ bhvStarRandomized,        "
        "/*acts*/ ALL_ACTS),"
    )


def ensure_randstars(script: Path) -> None:
    lines = script.read_text().splitlines()

    if already_done(lines, "bhvStarRandomized"):
        print(f"{script} already has a rando star")
        return

    the_return_line = get_first_return_line(lines)
    if not the_return_line:
        print(f"could not find return line in {script}")
        return

    response = input(f"should I do {script}?: ")
    if response.lower() != "y":
        return

    for i in range(3):
        object = rando_star(i)
        lines.insert(lines.index(the_return_line), object)

    script.write_text("\n".join(lines) + "\n")


def main(mode: str):
    for script in Path("./levels").glob("**/script.c"):
        if mode == "demon":
            ensure_demon(script)
        elif mode == "randstars":
            ensure_randstars(script)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("mode", choices=("demon", "randstars"))
    args = parser.parse_args()
    main(args.mode)
