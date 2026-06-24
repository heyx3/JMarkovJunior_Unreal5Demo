# JMarkovJunior_Unreal5Demo

An Unreal 5 Demo and reusable plugin, for integrating [MarkovJunior.jl](https://github.com/heyx3/MarkovJunior.jl).
See that repo for more information, but in a nutshell, MarkovJunior is an algorithm
  for procedurally generating grids -- 2D grids (images), 3D grids (voxels), and more.

The integration plugin runs MarkovJunior.jl as a standalone exe,
  using that project's [IPC protocol](https://github.com/heyx3/MarkovJunior.jl#through-the-ipc).

> *Currently we only support Windows, but I welcome PR's to add other platforms!*

## Integration Plugin

The integration plugin `JMarkovJunior` offers a new engine subsystem, `UJmjProcessManager`.
This subsystem runs *JMarkovJunior_IPC.exe* during startup and closes it on shutdown.

> *OS calls are used to ensure the IPC process never outlives the Unreal game, even with a hard crash.*

### API

> *NOTE: this interface is highly experimental and subject to radical change between versions*

The main interaction with JMarkovJunior is done through simple IPC functions, called through the subsystem.
For example:

* `ParseAlgorithm` tries to create a new algorithm given a source string.
* `DestroyAlgorithm` cleans up a parsed algorithm once you no longer need it.
* `StartAlgorithm` starts generating a new grid of pixels using a parsed algorithm.
* `DownloadGrid` gets the current state of a generator.

These are only some of the functions available.
All of them have detailed comments explaining their use.
If you don't clean up your algorithm states and parsed algorithms,
  they will persist in memory until the game/IPC process closes.

For simple use-cases, there are also higher-level functions you can make use of:

* `Generate2D` and `Generate3D` are for one-off uses of an algorithm.
A single call parses, generates, downloads the grid, and destroys the algorithm/state objects.
* To download a grid from an existing 2D or 3D state, use `DownloadGrid2D` and `DownloadGrid3D`.

### Development

For debug purposes the process manager will first look for an existing IPC connection running on your computer.
This allows you to run the IPC server through a normal Julia runtime, rather than a compiled exe,
  which is needed to do work on the server code.
In this case all the usual process-management stuff is skipped.