# Notes on Geant4 example B4

To be filled after the B4 runs.

Nothing below is an answer. Each question is open until the owner has run the
Geant4 example B4 (examples/basic/B4) and recorded what was observed.

## Questions

1. How does Geant4 seed events per thread in multithreaded mode?
2. How will we make the output independent of thread count?

## Why these matter

HITS.md requires `hits.arrow` to be ordered by event and, within an event, by
step order, with a result that does not depend on the number of worker threads.
That only holds if the random number stream of an event depends on the run
seed and the event id and not on the thread that ran it. Question 1 decides
whether that is already true; question 2 is what we do if it is not.

## To record for each run

- Date (from `date -u`), machine, operating system, architecture.
- Geant4 version and conda build string (`micromamba list geant4`).
- Exact command, number of threads, seed settings, number of events.
- What was compared (for example hit lists from 1 thread against N threads),
  and how.
- What was observed, copied from output, not paraphrased.

## Question 1: per-thread event seeding

Observations: (to be filled)

Source references (Geant4 file and class names, documentation pages): (to be filled)

## Question 2: output independent of thread count

Observations: (to be filled)

Options considered: (to be filled)

Decision: (to be filled by the owner)

## Effect on HITS.md and SIDECAR.md

(to be filled)
