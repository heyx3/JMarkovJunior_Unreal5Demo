# JMarkovJunior_Unreal5Demo

An Unreal 5 Demo and reusable plugin, for integrating [MarkovJunior.jl](https://github.com/heyx3/MarkovJunior.jl).
See that repo for more information, but in a nutshell, MarkovJunior is an algorithm
  for procedurally generating grids -- 2D grids (images), 3D grids (voxels), and more.

The integration plugin runs MarkovJunior.jl as a standalone exe,
  using that project's [IPC protocol](https://github.com/heyx3/MarkovJunior.jl#through-the-ipc).

> *The C++ code should support any platform, but only windows is tested, and I only built the underlying library for Windows.*

## Integration Plugin

The integration plugin `JMarkovJunior` offers a new engine subsystem, `UMarkovJuniorSubsystem`.
This subsystem launches *JMarkovJunior_IPC.exe* in the background during startup and closes it on shutdown.
Julia takes a moment to boot, so the first use of the plugin waits until the process is ready
  (see `StartupTimeoutSeconds` under [Settings](#settings)).

> *OS calls are used to ensure the IPC process never outlives the Unreal game, even with a hard crash.*

### API

> *NOTE: this interface is experimental and subject to moderate change between versions*

Everything is exposed to both C++ and Blueprints, through two objects:

* `UMarkovJuniorAlgorithm` is a parsed algorithm, which can be run any number of times.
  * Create one with the static function `Parse()`, passing the algorithm's source code. On a syntax error it logs the message and returns null.
  * Start a run with `StartRunning2D()`, `StartRunning3D()`, or the N-dimensional `StartRunning()`.
    Provide RNG seeds, and optionally an initial grid (pass an empty array to start with the algorithm's default fill color).
  * In C++ you can call `CleanUp()` to release it early; otherwise this happens when it's garbage-collected.
    Existing runs are not affected either way.
* `UMarkovJuniorRunState` is one run of an algorithm, which executes on its own worker thread.
  * `GetLastKnownStatus()` tells you whether it's `Running`, paused on a `TaggedEvent`, or `Completed`.
  * When paused or completed (check `CanReadGrid()`), the newest grid is available in `LastKnownPixels`
  and its size in `GetLastKnownResolution2D()`/`GetLastKnownResolution3D()`.
  Do not read the grid or its size while `Running`, as the worker thread may be reallocating it!
  * If the tagged event is mutable (i.e. the first char of its name is '!', stored in `jmj::ipc::MutableEventPrefix`),
  you are encouraged to *modify* the grid pixels.
  * `ResumeAfterEvent()` continues a run that's paused on a tagged event.
  If the event was mutable and you modified the grid, pass `true` to upload those changes.
  * `CancelRun()` stops a run early; if you want to download the final grid state on cancellation then pass `true`.
  * `WaitForEvent()` blocks the calling thread until algorithm completion (or optionally, the next tagged event).
    It's handy for tests and demos, but you probably don't want that in a shipping game.

Grid data is always X-innermost, one byte per cell.
`UJmjConstants` has helpers for interpreting cell values (colors, chars, and names) and for printing grids.

#### Tagged events

An `@event` in your algorithm pauses the run so that your game can look at the grid in the middle of generation.
The event's name is available as `LastNamedEvent` (or `GetLastNamedEvent()` in C++).

These events are one of the only times you may read the grid's contents.
Read `LastKnownPixels` and `LastKnownResolution` as desired.
Resume generation by calling `ResumeAfterEvent()`.

If the event's name starts with `!` (see `jmj::ipc::MutableEventPrefix`), it's a *mutable* event.
You are encouraged to modify (but not resize) the contents of `LastKnownPixels`,
  then call `ResumeAfterEvent(true)` to ensure those changes get sent back to the algorithm.

#### Lifetime

You don't *need* to clean anything up manually.
The run is cancelled (or just cleaned up) when the corresponding Unreal objects are garbage-collected,
  and of course the Unreal process ending also causes this library to clean up all its resources.

However, calling `CancelRun()` yourself is still a good idea when you know you're done with a run,
  because it cleans up the worker thread immediately.

You can still read the last known grid contents after cancelling a run,
  and even pass `CancelRun(true)` to re-download the grid state just before cancellation.

### Settings

Under *Project Settings > Plugins > JMarkovJunior* (stored in *DefaultEngine.ini*):

* `StartupTimeoutSeconds`: how long to wait for the IPC process before giving up and crashing. Values `<= 0` wait forever.
* `RandomizeNamedPipeForCommandlets`: if true (the default), commandlets talk to the library using a unique pipe name.
  This is needed because communication with the lib goes through an OS-wide mechanism (a "named pipe"),
  so multiple commandlets running at the same time would likely interfere and crash each other.

### Development

For debug purposes the subsystem will first look for an existing IPC server on your computer using the same pipe name.
This allows you to run the IPC server through a normal Julia runtime, rather than a compiled exe,
  which is needed to do work on the server code.

In this case all the usual process-management stuff is skipped -- the Julia process is not killed when Unreal dies.
