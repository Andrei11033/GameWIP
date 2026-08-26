@page logger_output Output

Normal output is selected with `Logger::Types::OutputMode`: `None`, `Console`, `File`, or `Both`.

Console uses Terminal and preserves its stdout/stderr severity routing. File output uses FileSystem and keeps the collision-safe timestamped filename
behavior. Debugger output and the fatal popup are emergency channels controlled separately by configuration.

File-only startup may fall back to Console when configured. `Both` naturally retains Console when File setup fails. Initialization exposes
requested/effective normal output and the original setup failure directly through `Types::Init::Result`.

At runtime a failed Console or File sink is disabled for the current Logger initialization. `getOutput()` and `getHealth().effectiveOutput` reflect
the surviving normal sink set. Reinitialization is the explicit retry boundary.
