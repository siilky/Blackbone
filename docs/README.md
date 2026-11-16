# Documentation policy

This document owns where repository knowledge lives and how it stays current. Each durable fact has one canonical owner; other documents link to it instead of restating it.

## Locations

| Location | Contents | Tracking |
|---|---|---|
| `AGENTS.md` | Entry point: structure, build commands, conventions, and pointers to agent configuration | committed |
| `docs/agents/` | Agent workflow, delegation, issue tracker, and domain-doc configuration | committed |
| `GLOSSARY.md`, `docs/adr/` | Domain terms and recorded decisions, created when a term or decision is settled ([domain docs](agents/domain.md)) | committed |
| `docs/TODO.md` | Accepted future work, created with its first entry | committed |
| `.scratch/` | Specs, tickets, plans, and session handoffs ([issue tracker](agents/issue-tracker.md)) | local, ignored by Git |

Incident logs, raw command output, transient test counts, and exploratory notes stay in the session or in `.scratch/`.

## Authority

Durable documents state the accepted current contract. Current source and focused tests establish what is implemented; an accepted spec or an explicit user decision establishes what is intended; a plan or a passing test alone does not. When these disagree, investigate the discrepancy and describe planned behavior as planned.

## TODO entries

`docs/TODO.md` lists accepted future work only. Each entry has:

- `Problem`: the concrete defect, risk, or limitation, its causal mechanism, and the affected behavior;
- `Action`: the remaining work, with the constraints and invariants needed to make it actionable;
- completion criteria a reviewer can check.

An entry stands without any `.scratch/` file. Completing a task removes its entry in the same change; partial completion rewrites the entry to describe only what remains. Implemented behavior, history, and status narratives belong in the canonical documents or in Git history.

A deferred finding from a review, audit, or delegated worker leaves the report: accepted work goes to `docs/TODO.md`, work a later planned block resolves goes to that plan, and a candidate the user has not accepted goes to the user as a question.

## Completion audit

After an implementation, a meaningful follow-up, or a review that exposes drift, check whether `AGENTS.md`, `docs/agents/`, the glossary, ADRs, or `docs/TODO.md` need an update. Update the smallest set of owners and replace duplicates with links. When nothing changed, report the audited locations and why no edit was needed.

Before completing a documentation change, verify relative links and heading anchors, search for obsolete terms the change retires, and run `git diff --check`.
