# Handoff: the Study (stages 16-19)

For the Claude Code session that carries the Study on from GitHub. Written 2026-09-30 in the local folder
(`C:\Users\phill\Downloads\Depth`), after the Red Tide / Trawl branch was merged into `master`. Read `CLAUDE.md`
first (its two Study sections), then `study/INGEST.md` (the course-pack procedure), then this file.

## 0. What the Study is
A room below the salon's hatch (Master Reference pages 35-60, "the Study hatch"). It is a real study tool for the
player's own university courses inside the game:
- **Stage 16, the shell (done):** the descent, the desk rail with three drawers, focus mode, the chronometer, the
  soundscape mixer, and two scenes (the Captain's Study, the Lamplight Lounge).
- **Stage 17, course packs (engine done, three units built):** a verified question bank per course, built from the
  player's materials by the procedure in `study/INGEST.md`.
- **Stage 18, the problem engine (not started):** serving problems, checking answers, hints, mastery, practice modes.
- **Stage 19, the Live Tutor (not started, optional):** a switch that calls the Claude API.

## 1. The code (all committed)
| File | What |
|---|---|
| `src/study.cpp`, `study_scenes.cpp`, `study_audio.*`, `study_data.*`, `study_test.cpp` | Stage 16: the room, the scenes, the soundscape, every tuning number, `--study-audio-test`, `--study-motion-audit`, `--study-save-test` |
| `src/json.*` | The shared JSON reader (also Red Tide's and the Trawl's): accessors `Str0` / `I` / `F` / `Bool0` / `Num0`, enum `Json::Null..Obj`; the course packs also use `ParseJson(text, out, err)`, `LoadJsonFile(path, out, err)`, `WriteJson`, `JStr`, `JNum` |
| `src/expr.*` | The answer parser and numerics: implicit multiplication, `sin^2 x`, `ln|x|`, unicode; `Equivalent` / `EquivalentUpToConstant` at random points; `Derivative`, `Derivative2`, `Integrate` (incl. infinite bounds). `--expr-test` (54 checks) |
| `src/course.*` | `LoadCourses` (for the Courses drawer) and the tools: `--course-verify [course] [unit]`, `--course-report`, `--course-expand`, `--course-seed-test` |
| `study/INGEST.md` | The whole ingestion procedure: steps, the six gates, quarantine, the session procedure, the item schema and every `check` type |
| `study/CHANGELOG.md` | One line per pack build |
| `tools/pdfpages.ps1` | Renders PDF pages to PNG with OCR (Windows.Data.Pdf, **Windows only**; on Linux use `pdftoppm -r 110 -png` and `tesseract`) |
| `tools/merge_gates.ps1` | Merges sub-agent gate files into `pack/verify/Uxx.gates.json` (PowerShell; runs under `pwsh` on Linux) |

Build on Linux as the Red Tide handoff says (`cmake --build build_rel`). The checks for the Study:
```
depth.exe --expr-test
depth.exe --course-verify MATH_CALC2_F26         (must end "0 errors")
depth.exe --course-seed-test                     (20 of 20 planted errors caught, 0 good items rejected)
depth.exe --study-audio-test / --study-save-test / --study-motion-audit study 20
depth.exe --shots shots study                    (then look at shots/study*.png)
```

## 2. The course materials (NOT in the repo: read this)
`study/courses/`, `study/reports/`, `study_save.txt` and `Study_Hatch_Reference/` are gitignored, and **the GitHub
repo is public**. The player's course materials therefore are not pushed with the code. What exists locally:

| Local path | What | Can it go to GitHub? |
|---|---|---|
| `study/courses/MATH_CALC2_F26/pack/` | The course pack: `course.json`, `objectives.json`, `coverage.md`, `conventions.json`, `STATUS.md`, `review_queue.md`, and per unit `units/`, `knowledge/`, `anchors/`, `blueprints/`, `items/`, `instances/`, `verify/` (gate results) | Only with the player's say-so (anchors paraphrase the instructor's problems) |
| `study/courses/MATH_CALC2_F26/source/lectures/`, `problem_sets/`, `syllabus/` | The instructor's Class 2 and 3 notes, the Class 1 activity, the syllabus | Only with the player's say-so, and better in a private repo |
| `study/courses/MATH_CALC2_F26/source/textbook/openstax_calc2.pdf` | OpenStax Calculus Volume 2 (CC BY 4.0; 124 MB, over GitHub's 100 MB file limit) | No need: download it from openstax.org |
| `.../textbook/stewart_9e.pdf`, `fundcalc.pdf`, `cheat_sheet.pdf`, and `Study_Hatch_Reference/` (1 GB of textbooks) | Copyrighted books | **Never** |
| `study/courses/MATH_CALC2_F26/work/` | Page renders and OCR of the sources | Regenerate from the sources |
| `study/courses/_seeded_test/` | The seeded-error fixture (30 items, 20 planted errors) | Yes if the pack goes; it's synthetic |

If the pack isn't in your checkout, ask the player how they want to get it to you (for example, making the repo
private and pushing it, or attaching it). Don't rebuild U01-U03 from scratch: they passed every gate.

**Free sources you can fetch yourself** (course.json lists them): Active Calculus 2e
(https://activecalculus.org/single2e/, sections 5.4-7.6: the course's main text), the UVic differential equations text
(https://web.uvic.ca/~tbazett/diffyqs/), APEX Calculus (https://opentext.uleth.ca/apex-calculus/), Active Calculus 1e
chapter 8 (https://activecalculus.org/single/), and OpenStax Calculus Volume 2 (https://openstax.org/details/books/calculus-volume-2;
"PDF page = book page + 8"). Only public textbook URLs may be fetched: never send course content anywhere.

## 3. The course: MATH_CALC2_F26
- MATH 1272 Calculus II, University of Minnesota, Fall 2026 (course.json has the details).
- Exams: **Midterm 1 on 2026-10-07 (U01-U07)**, Midterm 2 Nov 4 (U08-U13), Midterm 3 Dec 2 (U14-U17), final Dec 17.
- No calculators on exams, so every answer is exact (conventions.json).
- The instructor stresses strategy choice, "set up, don't evaluate", and error analysis.

18 units are planned in course.json. State:

| Unit | State |
|---|---|
| U01 Integration by parts, partial fractions | Built: 71 items (24/24/13/10), 1 quarantined, 4 templates (505 instances) |
| U02 Area, arc length, volume | Built: 60 items (20/20/10/10), 2 templates; 4 gate rounds |
| U03 Density, mass, center of mass, work, force | Built: 60 items, 2 templates; 3 gate rounds. Includes cables and pumping (AC2 6.4); 2D centroids are out of scope |
| **U04 Improper integrals** | **Next.** AC2 6.5, OSC2 3.7 (PDF pages 296-314) |
| U05 Intro to differential equations | AC2 7.1, OSC2 4.1 (mixed: some conceptual items) |
| U06 Slope fields, Euler's method | AC2 7.2-7.3, OSC2 4.2 (`euler` check type exists) |
| U07 Separable equations, modeling | AC2 7.4-7.5, OSC2 4.3 (`ode` check type exists) |
| U08-U18 | Planned |

**No instructor notes exist after Class 3.** The anchors (the tier calibration) must come from the instructor's
coursework; for U04-U07 there is none yet. Ask the player whether Class 4+ notes or problem sets exist. If not, the
plan agreed so far: build from the textbooks, take anchors from Active Calculus's own activities and exercises
(labelled as textbook anchors), and say so in STATUS.md.

## 4. How a unit is built (the procedure that worked, U01-U03)
`study/INGEST.md` is the authority; this is the practical order.
1. **Read every source page** for the unit. Look at the page images: OCR drops the math. Log every page in
   `coverage.md` (`used` / `queued` / `out_of_scope`); `--course-verify` fails on an unlogged page.
2. **Write** `objectives.json` entries, `units/Uxx.json` (about 10 skills), `knowledge/Uxx.json` (cards K1.. and
   misconception cards M1..; mark ones from Claude's own knowledge `"outside": true` with a note), `anchors/Uxx.json`
   (the coursework, with steps/form/cue), `blueprints/Uxx.json` (every skill: 2 Easy, 2 Medium, 1 Hard, at least 1
   Challenging; at least 10 per tier), then `items/Uxx.json` and 2 templates. Set the unit to `built` in course.json.
3. **Compute every key by hand, then give each a `check`** (a second way), and list real `wrong` answers. Check types:
   `antiderivative`, `definite`, `diverges`, `equal`, `value`, `ode`, `euler`, `series`, `derivative`, `integral`,
   `arclength`, `solves` (INGEST.md explains each).
4. `--course-expand MATH_CALC2_F26 Uxx`, then `--course-verify MATH_CALC2_F26 Uxx` until the only errors are the
   missing gates 2-4.
5. **Sub-agent gates** (run in parallel, background):
   - two **blind solvers**, who see only a packet of prompts and choices (no keys), split Easy+Medium / Hard+Challenging
     plus two instances per template; answers in `{answers, notes, work}` (notes only for real ambiguity)
   - one **grounding** reviewer (keys, every step, every mistake text, every citation opened)
   - two **independent adversarial** reviewers (wrong keys, second defensible answers, ambiguity, leaks, tier,
     duplicates, textbook copies). Both must pass an item.
   Merge with `tools/merge_gates.ps1`, fix what they block, and re-gate only the changed items in the next round
   (blind + grounding + both reviewers). Rebuild the gates file from every round, latest verdict per item winning.
6. Finish when `--course-verify MATH_CALC2_F26` says 0 errors. Update `pack/STATUS.md`, `study/CHANGELOG.md` and the
   CLAUDE.md line, and commit (the pack itself stays uncommitted unless the player says otherwise).

**Lessons from U01-U03** (reviewers block these every time):
- Every mistake text must reproduce its wrong answer exactly: compute each wrong answer from the named slip.
- Write the key as choice `a` (the UI shuffles), but keep the choices similar in length and detail; put explanations
  in the solution, not the key.
- A Hard names its twist from the list (extra step, unfamiliar setup, earlier unit, word problem, returns, improper,
  setup only) and its **prompt must not name the method**. A Hard that mirrors an anchor or another item is blocked.
- A Challenging item combines three or more skills without scaffolding (parts that name each step are a Hard).
- No near-duplicates inside the unit (check against every other item and every template instance), no textbook copies.
- Every `check` must equal the whole key (no partial checks); choice items carry no `check` (label them `sourced`).
- Prompts must be unambiguous (where x is measured from, depth vs position, weight vs mass, "over the top").
- Sub-agents sometimes stall after writing their file: check the file before re-running.

## 5. What to do next, in order
1. **U04-U07** before Midterm 1 (2026-10-07), with the procedure above (after sorting out the materials, section 2).
2. **Stage 17's remainder** (Master Reference p. 55-56 "Definition of Done"): review mode, the report button,
   quarantine and the report-first session rule working end to end in the Courses drawer.
3. **Stage 18, the problem engine** (Master Reference pp. 56-60), `problems.cpp`, every threshold in the Study data file:
   - **Tiers** as INGEST.md defines them; record first-try unassisted accuracy per tier (targets about 85/65/45/25%)
     and flag items far outside their band for reclassification.
   - **Answer checking:** numeric (relative tolerance, default 0.5%), numeric with units (convert equivalent units;
     flag units separately from value), expression (expr.cpp at 8 random points; antiderivatives up to a constant),
     choice and multi-select (exact; show the distractor's `why`), ordering and matching (partial credit per
     position), short text (keyword rubric with synonyms), free response (the player ticks rubric points, or sends it
     to the tutor). Matching a listed `wrong` answer names the mistake.
   - **Hints:** the item's three hints (nudge, setup, nearly there), then the worked solution (steps labelled by
     skill, a "why" on non-obvious steps, the common mistake and the page to reread). Any hint marks the attempt
     assisted.
   - **Modes:** unit practice (unit and tier), mixed review (spaced, weighted to weak skills), weak spots (skills
     below the mastery line, starting Easy), exam simulation (timed, the exam's tier mix, no hints, scored at the end
     by skill).
   - **Mastery:** 0-100 per skill from recent attempts (unassisted correct counts most, harder tiers count more); a
     brass pressure gauge per unit in the Courses drawer; Challenging recommended once every skill passes the Medium
     line. A missed item returns after 1, 3, then 7 days and leaves the queue after two unassisted correct answers in
     a row (data values).
   - **Feedback:** correct = a green lamp on the desk rail, a warm brass chime (3 variants), a short line ("Good work",
     "Clean solve", "That's the one"); every 5 in a row the ship's bell rings softly; incorrect = an amber lamp (never
     red), a soft low tone, "Not quite", and the next hint offered; after 2 misses the solution is offered. A session
     summary card. Every sound and message can be switched off; nothing flashes or shakes.
   - **Done when:** all tiers, answer forms, hints, solutions and the four modes work on the first two courses;
     `--study-check-test` passes a fixture of equivalent and non-equivalent answers (expressions, units, synonyms,
     antiderivatives differing by a constant); `--study-sim <course> <days>` confirms review days and mastery.
4. **Stage 19, the Live Tutor** (optional, off by default): Claude API over HTTPS (libcurl), key stored locally. New
   problem (computable ones must pass the checker before being shown; conceptual ones marked "unverified"), grade my
   answer against the rubric, explain it differently. The tutor receives only the current item, the unit's skill
   list and the player's answer: never the pack or the sources.

## 6. Ground rules the player set
- Follow the Master Reference's order. C++ checks only (no Python or SymPy). Sub-agents for the blind-solve and
  adversarial gates.
- Course content stays private: never send it over the network, never commit copyrighted books.
- Commit locally; push only when the player asks.
- The player's other folders (Physics, Chemistry, Circuitry) are later courses.
