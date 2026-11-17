# Delegated implementation

The root session keeps ambiguity, contracts, test-case selection, scheduling, review adjudication, commits, and acceptance. Bounded implementation goes to fresh-context workers through task packets. [Agent skill configuration](../../AGENTS.md#agent-skill-configuration) decides when this applies; [the workflow](workflow.md) still governs builds, workspace, and review.

## Roles

| Role | Owns | Model and effort |
|---|---|---|
| root | contracts, test cases, packets, integration, review adjudication, commits | this session |
| implementer | code and test bodies inside one packet | standard tier, high effort |
| implementer, hard lane | the same for remote execution, thread hijacking, manual mapping, WOW64 barrier, syscall, and hook internals | strongest tier, high effort |
| scout | read-only discovery | by block weight below |
| reviewer | one compact review of the integrated patch | by block weight below |
| verifier | acceptance checks on the frozen final state | standard tier, medium effort |

Workers dispatch no agents and leave changes uncommitted. Role definitions carry an ordinary block's defaults; this repository's specifics reach the workers through [the workflow](workflow.md). [The local environment](workflow.md#local-environment) maps the roles, tiers, and operations below onto the client.

- Start every worker with a fresh context under its role's definition and the model and effort from these tables. A forked context inherits the root's context and model, which defeats both the fresh context and the routing. When the client cannot load a role definition by name, include the definition's instructions in the packet.
- Continue a worker through the client's continuation operation on its existing agent; a new dispatch starts cold.
- A role's no-edit and no-subagent rules are enforced only when the client restricts the worker's tools; otherwise they are instructions, so do not claim that a reviewer or verifier ran in a read-only sandbox.

Pass model and effort on every reviewer and scout dispatch, sized by the block:

| Block weight | Examples | Model | Effort |
|---|---|---|---|
| Hard | the hard-lane areas above, cross-process memory layout, concurrency | strongest tier | high |
| Ordinary | a localized fix with tests | standard tier | medium |
| Light | docs, a test-only or wording change, a file search | standard tier | low |

Without subagent support, run the same stages sequentially and disclose the missing independent verification.

## Quality guard

Delegation trades the root's tokens for packet precision; a weaker model degrades decisions far more than typing within a fixed frame.

- **Packet test.** If a packet cannot be written without solving the task, the root implements it instead.
- The packet pins signatures, ownership, invariants, and a reference pattern in existing code ("follow `RemoteExec::ExecInAnyThread`"). The root chooses test cases, boundaries, and inverse cases; the implementer writes their bodies.
- Split work along real ownership boundaries; create no abstractions or files just to occupy workers.
- Review findings return to the implementer that wrote the code through the client's continuation operation above. When that context is gone, a new implementer receives the findings together with the prior diff.

## Prepare and dispatch

1. Settle the requested outcome, existing invariants, acceptance checks, and every unresolved contract decision before any writer starts.
2. For a large unfamiliar batch, dispatch scouts on the open architecture or verification questions.
3. Find every consumer of each contract the batch changes: library callers, tests, samples, and the ctypes signatures in `src/PythonicBlackBone/`. Search `src/BlackBoneTest/` by text as well, because symbol-reference search can miss test consumers. Assign each consumer to an owned path or record why it stays unchanged.
4. Record dependencies and exclusive file ownership in a compact table. Launch only ready, independent tasks.
5. All workers share the root's checkout, because the integrated base usually includes uncommitted changes. Each writer edits only its owned files; overlapping work is serialized with an explicit ownership transfer. Builds and tests run one at a time under [Build and test](workflow.md#build-and-test); a worker whose build fails on another lane's files reports the failure.

Each packet contains:

```text
Task and acceptance criteria:
Base revision and existing working-tree changes:
Owned paths / forbidden paths:
Changed-contract consumers and their owners:
Required invariants, user decisions, relevant instructions:
Reference pattern and authoritative references:
Known locations (file:line from scouts and root reading):
Focused checks as exact command lines, each build before the test it feeds:
Return: outcome; files changed; commands with exit status and relevant output;
        invariant evidence; blockers; cross-lane contract changes.
```

## Integrate and verify

1. Workers run their focused checks. A worker's PASS is evidence for its scope, not integrated acceptance.
2. The root assembles the batch and runs cross-lane checks.
3. One `reviewer` reviews the integrated patch, including in-scope uncommitted changes; the root adjudicates under [Review](workflow.md#review) and sends accepted fixes back to their implementers.
4. Once fixes are applied and the tree is frozen, a `verifier` that authored no implementation runs the acceptance checks. The root supplies exact commands, platforms, configurations, and expected outcomes. A verifier running beside review or fixes certifies a stale state.

Before claiming success, the root checks each piece of evidence for the tested revision or patch identity, command, exit status, relevant output, and coverage; a bare PASS is insufficient. Changes after the gate invalidate the affected evidence; rerun those checks before acceptance. If a verifier reports PASS despite a failed check, stop relying on its certificates and block acceptance. A crashed test host is a failure of the gate, distinct from an assertion failure.
