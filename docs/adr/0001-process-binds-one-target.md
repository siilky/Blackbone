# A Process object binds one target for its whole lifetime

Status: accepted.

## Context

`Process` is an aggregate: its parts (`memory()`, `modules()`, `threads()`, `remote()`, `mmap()`, `hooks()`, `localHooks()`, `nativeLdr()`, `memory().remote()`) hold back-references to `Process` and to each other, and keep state bound to the attached target. `Detach` followed by `Attach` reuses the same object for another target, which produces three classes of misuse:

- references returned by accessors either dangle (`memory().remote()` is destroyed in `Detach`) or point to a part that still holds the old target's state;
- per-target state survives re-attach: the hijacked RPC thread, the `NtLdr` heap base and node map, a local hook restored into the new process;
- `Detach` calls every part's `reset()`, so any consumer that attaches links the cleanup code of parts it never uses.

Objects handed out by the parts (`MemBlock`, `Thread`/`ThreadPtr`, `RemoteFunction`) also keep raw pointers or references to the parts, and nothing states that they must not outlive the `Process`.

Rejected alternatives:

- Returning `shared_ptr` to parts: a copy that outlives the `Process` still holds references to destroyed siblings, and one that outlives a re-attach works on the new target with old state. Making it safe requires shared ownership of all process state.
- Generation-checked handles: detect misuse only at run time through assertions, because the API reports errors through `NTSTATUS` rather than exceptions, and every part access and every handed-out object needs the wrapper.
- Attaching in the constructor (`Process p( pid )`): forbids a second attach at compile time, but a constructor cannot return `NTSTATUS`, so a failed object would carry its status for a later `status()` check. `Process` would become the only object whose construction fails, and `CreateAndAttach` would need a separate parameter type to avoid overload conflicts with attach-by-name.
- A factory returning `call_result_t<std::unique_ptr<Process>>`: removes the unattached state, but every consumer pays a heap allocation and a double indirection.

## Decision

Error model for `Process` and all its parts: construction cannot fail, every step that can fail is an explicit operation returning `NTSTATUS` or `call_result_t`, and a call whose precondition does not hold returns an error status instead of acting on invalid state.

1. `Process` is default-constructed without touching any process. `Attach` and `CreateAndAttach` return `NTSTATUS` and succeed at most once per `Process` object; a later call returns `STATUS_ALREADY_INITIALIZED` without touching the attached target or starting a process. A failed attempt leaves the object unattached, so it can be retried. Working with another process needs a new `Process` object (for example through `std::optional<Process>::emplace` or `std::unique_ptr<Process>`).
2. Accessors never fail and never operate on the target: they only create the part on first use. Setup that can fail (RPC environment, driver mapping, debugger attach for hooks) stays an explicit operation of the part that returns a status.
3. An operation whose precondition does not hold returns an error status. On an unattached `Process` that is `STATUS_INVALID_HANDLE` where the operation reaches the system or checks the process ID; an operation that first looks up a module or export fails with the status of that lookup. Remote execution without the RPC environment it needs returns `STATUS_INVALID_PARAMETER`. The unattached `Process` keeps a native subsystem over a null handle, so operations that reach the system get its handle error; only operations that would act through process ID 0 check it explicitly. An operation without a status in its signature returns an empty result. Before `Attach`, `barrier()` describes a target that is not WOW64, that is, of the OS bitness; `pid() != 0` tells whether the object is attached.
4. `Detach` is removed. Destroying the `Process` detaches: its parts are destroyed while the process handle is still open, then the handle is closed.
5. A reference returned by any `Process` accessor, including nested ones such as `memory().remote()`, stays valid until the `Process` is destroyed. No operation invalidates it earlier. The exception is the native subsystem returned by `core().native()`: `Attach` replaces it, so a pointer obtained before `Attach` must not be used after it. `barrier()` returns a copy for the same reason.
6. Objects obtained from a `Process` (`MemBlock`, `Thread`/`ThreadPtr`, `RemoteFunction`, and any other object holding a reference to a part) must not outlive it.
7. A `Process` object and its parts are not thread-safe: concurrent use, including the first call of a lazy accessor, needs external synchronization.
8. `Process` stays non-copyable and non-movable, since its parts hold its address.

## Consequences

- No part ever sees a second target, so the re-attach defects listed above disappear without per-part reset logic.
- Parts are destroyed only in `~Process`, through deleters bound in the lazy accessors, so attaching no longer links the cleanup code of unused parts.
- Code that re-attaches one object, or calls `Detach`, must create a new `Process` or end the object's scope instead. The DLL ABI changes.
- A second attach is caught at run time through its status, not at compile time; this is the price of keeping one error model without exceptions.
- Operations that today act on a null handle or a missing setup without checking must be brought to an error status.
- The destructor reports no errors; `Detach` already always returned `STATUS_SUCCESS`.
