"""Builds the audio context table with every sound the scenarios use.

Each row is a sound file, how it is reported, a name and a description. The
description is left empty: until it is written the plugin says the name. Rows
already described in the existing table are kept as they are, so running this
again only adds what is missing.

    python Tools/audio/build_audio_context_template.py

Reads the usage and track tables under docs-local/research/audio and writes
integration-assets/neuro-assets/audio_context.tsv.
"""
import csv
import os
import re

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
AUDIO = os.path.join(ROOT, "docs-local", "research", "audio")
TABLE = os.path.join(ROOT, "integration-assets", "neuro-assets", "audio_context.tsv")

HEADER = """# What Neuro is told she hears. One row per sound file, fields separated by tabs:
#
#   file    report    name    description
#
# description  How the sound is put into words: "You hear <description>."
#              Empty means not written yet, and the name is said instead, in
#              brackets: "You hear [se171]."
# name         What to call the sound until it is described. Music carries its
#              soundtrack title; effects only have their file name.
# report       When to speak of it:
#                start  when it starts
#                both   when it starts and when it stops
#                off    never
#              Empty takes the usual: music and looping sounds "both", a
#              one-shot effect "start" -- nothing tells us when one of those ends.
#
# A sound missing from this table is still reported, by its file name.
"""


def read_csv(name):
    with open(os.path.join(AUDIO, name), encoding="utf-8-sig", newline="") as f:
        return list(csv.DictReader(f))


def track_titles():
    titles = {}
    for row in read_csv("extra-music-track-map.csv"):
        title = (row.get("SoundtrackTitle") or row.get("ButtonLabel") or "").strip()
        if title:
            titles[row["Storage"].lower()] = title
    return titles


def music_name(file, titles):
    if file in titles:
        return titles[file]
    # bgm01b, bgm03s, bgm04sa: a variant of the numbered track.
    match = re.match(r"(bgm\d+)([a-z]+)$", file)
    if match and match.group(1) in titles:
        suffix = match.group(2)
        kind = "short version" if suffix == "s" else "variant " + suffix
        return titles[match.group(1)] + ", " + kind
    return file


def existing_rows():
    rows = {}
    if not os.path.exists(TABLE):
        return rows
    with open(TABLE, encoding="utf-8-sig") as f:
        for line in f:
            line = line.rstrip("\r\n")
            if not line or line.startswith("#"):
                continue
            fields = line.split("\t")
            fields += [""] * (4 - len(fields))
            rows[fields[0].lower()] = fields[:4]
    return rows


def main():
    titles = track_titles()
    kept = existing_rows()
    music, effects, unreal = [], [], {}
    for row in read_csv("scenario-audio-usage.csv"):
        file = row["Name"].strip().lower()
        if row["InInventory"] != "True":
            # A name the scenarios use that has no file behind it: nothing
            # plays, so nothing is said.
            unreal[file] = [file, "off", "", ""]
        elif row["Type"] == "BGM":
            music.append([file, "", music_name(file, titles), ""])
        else:
            effects.append([file, "", file, ""])

    lines = [HEADER]
    sections = (
        ("# Music", music),
        ("# Sound effects", effects),
        ("# Named by the scenarios, but no such file exists", list(unreal.values())),
    )
    for title, rows in sections:
        lines.append(title)
        for row in sorted(rows):
            old = kept.pop(row[0], None)
            if old:
                # Whatever a person wrote wins over what is generated.
                row = [row[0], old[1] or row[1], old[2] or row[2], old[3]]
            lines.append("\t".join(row).rstrip("\t"))
        lines.append("")
    if kept:
        lines.append("# Not in the scenario tables")
        for file in sorted(kept):
            lines.append("\t".join(kept[file]).rstrip("\t"))
        lines.append("")

    with open(TABLE, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines))
    print(f"{TABLE}: {len(music)} music, {len(effects)} effects, "
          f"{len(unreal)} without a file, {len(kept)} kept as is")


if __name__ == "__main__":
    main()
