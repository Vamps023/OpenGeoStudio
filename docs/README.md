# OpenGeoStudio - Beginner's Guide (Start Here)

Welcome! If you have **never written code** but want to understand what this
project is and how it works, you are in the right place. These documents
explain everything in plain English, with everyday analogies.

**Think of this repository as a kitchen:** the folders are cupboards, the
source-code files are recipes, and the program you run (OpenGeoStudio.exe)
is the finished dish. These docs give you the full tour.

## Suggested reading order

| # | Document | What you will learn | Time |
|---|----------|--------------------|------|
| 1 | [01-what-is-this-app.md](01-what-is-this-app.md) | What the program does, in one page | 5 min |
| 2 | [02-the-five-workspaces.md](02-the-five-workspaces.md) | The 5 main screens a user sees | 10 min |
| 3 | [03-tour-of-the-folders.md](03-tour-of-the-folders.md) | What every folder is for | 15 min |
| 4 | [04-how-the-app-works-inside.md](04-how-the-app-works-inside.md) | What happens behind the screen | 10 min |
| 5 | [05-glossary.md](05-glossary.md) | Every technical word, translated | reference |
| 6 | [06-build-run-and-test.md](06-build-run-and-test.md) | How the app is built and checked | 10 min |
| 7 | [07-file-formats.md](07-file-formats.md) | The file types it reads and writes | 5 min |

## The absolute basics, in 30 seconds

- **OpenGeoStudio** is a **Windows desktop app** for designing
  **terrain, roads, and railways on top of real-world maps**.
- It is written in a language called **C++** using a toolkit called
  **Qt 6** (the same toolkit behind many desktop applications).
- This `docs/` folder contains **documentation**, not the program itself.
  The program's code lives in `src/` (short for "source").
- The current version is **1.0.0**, and the project is open source under
  the **MIT license** (free to use, change, and share).

## Words you will see everywhere

| Word | Meaning in this project |
|------|------------------------|
| Repository ("repo") | The whole folder of project files, tracked in Git |
| Source code | Text files written by humans; the instructions for the computer |
| Build | Turning source code into the actual .exe program |
| Workspace / Studio | One of the 5 main screens in the app |
| CRS | Coordinate Reference System - how a place on Earth gets its numbers |

New words are always explained the first time they appear, and all of them
are collected in the [Glossary](05-glossary.md).