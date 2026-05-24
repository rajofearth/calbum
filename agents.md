# Context Map: High-Performance Native Windows Development

To build applications that load instantly, consume negligible memory, and process vast amounts of data (like files or system processes) without stuttering, you must bypass standard frameworks and interact directly with the operating system and hardware.

This map divides the required knowledge into six core domains.

## 1. System Foundation & OS Interaction (The Win32 API)

This is how your application talks to Windows.

- **The Application Lifecycle**: Understanding the `WinMain` entry point and the fundamental lifecycle of a Windows desktop app.
- **The Window Class & Creation**: How Windows registers your application's "type" and asks the OS to allocate a window on the desktop.
- **The Message Pump (Event Loop)**: The heart of the application. How Windows sends hardware interrupts (mouse movements, key presses, window resizing) to your app as a queue of messages, and how you dispatch them.
- **Window Procedures (WndProc)**: The central switchboard function that receives and handles every single OS event.

## 2. Memory & Data Architecture

High-performance apps rarely use standard `malloc()` or `free()` during the main loop, as OS-level memory allocation is slow and causes fragmentation.

- **Virtual Memory vs. Physical Memory**: Understanding how Windows manages memory pages.
- **Custom Allocators**:
  - **Arena/Linear Allocators**: Grabbing a massive chunk of memory from Windows at startup and linearly distributing it to your app. Clears instantly without looping through individual `free()` calls.
  - **Pool Allocators**: Pre-allocating fixed-size chunks for things you create/destroy rapidly (like UI elements or file representations).
- **Data-Oriented Design (DOD)**: Structuring data for the CPU cache. Instead of "Arrays of Objects" (OOP style), using "Structures of Arrays" so the CPU can process thousands of files or tasks in a single cache line.

## 3. UI Architecture (Custom Immediate Mode)

Instead of asking Windows to draw buttons and menus, your app draws pixels directly and calculates interactions on the fly.

- **Immediate Mode GUI (IMGUI) Paradigm**: The concept of defining and evaluating UI state in a single pass during the main loop, rather than storing a massive tree of UI objects in memory.
- **State Management**: Tracking UI state conceptually using IDs. (e.g., Which item is currently *Hot* (hovered) and which is *Active* (clicked)?).
- **Layout Systems**: Building your own mathematical logic to calculate padding, margins, flex-boxes, and text wrapping based on the current window dimensions.
- **Clipping (Scissor Testing)**: Mathematical bounds-checking to ensure you don't draw UI elements that have scrolled off the screen or outside their designated panels.

## 4. Graphics & Rendering

How you translate your UI calculations into actual pixels on the monitor.

- **The Prototyping Phase (GDI)**: Using the Windows Graphics Device Interface for initial testing (drawing basic rectangles and text via the CPU).
- **Hardware Acceleration (The Goal)**: Shifting rendering to the GPU using DirectX 11/12 or OpenGL. This allows 144hz+ smooth scrolling even with thousands of items on screen.
- **Batching / Vertex Buffers**: Sending UI drawing commands to the GPU in one massive batch (e.g., "Draw these 500 rectangles") rather than one at a time.
- **Text Rendering/Rasterization**: The most complex part of custom UI. You must learn how to load a font file (`.ttf`), turn characters into bitmaps (using a library like `stb_truetype` or FreeType), store them in a "Texture Atlas" on the GPU, and calculate kerning/spacing to draw strings of text.

## 5. Heavy Lifting (I/O and Multi-threading)

This is what separates basic apps from tools like FilePilot. Reading a whole hard drive synchronously will freeze your app.

- **Asynchronous I/O**: Never blocking the main UI thread. If you search a drive, the UI must keep rendering at 60+ FPS.
- **Win32 File Management**: Using low-level APIs (`CreateFile`, `FindFirstFileEx`, `FindNextFile`) rather than standard C library functions.
- **I/O Completion Ports (IOCP)**: The most efficient way in Windows to handle thousands of simultaneous read/write operations (crucial for rapid file indexing).
- **Worker Threads & Synchronization**: Creating a pool of background threads to process data. Learning about Mutexes, Semaphores, and Atomic operations to safely pass data from background search threads back to the main UI thread.

## 6. Development Pipeline & Tooling

How you build, test, and measure the application.

- **Single Translation Unit (Unity Builds)**: Including all your `.c` files into one master `.c` file before compiling. This drastically speeds up compile times (often under 1 second) and allows the compiler to heavily optimize the code.
- **Minimal Dependencies**: Relying strictly on OS headers and single-file, header-only libraries (like the `stb` libraries for image loading or font parsing) to prevent bloat.
- **Profiling**: Using tools to measure exactly how many milliseconds a function takes. Performance isn't guessed; it is measured.

## 7. The Engineering Philosophy

To maintain a high-performance codebase, coding must be intentional, rigorous, and forward-looking.

- **Simplicity Above Complexity**: Always prefer the direct, boring, and minimal approach. If there is a "code judo" move that eliminates entire classes of complexity by restructuring, take it. Do not normalize architectural drift or allow code spaghetti to accumulate.
- **Holistic Problem Solving**: Do not just apply localized "band-aid" patches to fix symptoms. Think intellectually about the root cause in the context of the entire codebase and implement the *proper* solution that betters the overall architecture.
- **Deep Code Quality**: Measure twice, cut once. Keep functions focused, strictly enforce type safety (avoiding "magic numbers" and unstructured data), and refuse to let files grow into unmanageable 1000+ line monoliths. The codebase should read cleanly and deliberately.
- **Struct Miniaturization (Zero-Bloat Data)**: Never store fixed-size arrays (like `char path[1024]`) inside core structs that scale with item count. A seemingly harmless 2KB buffer bloats to 20MB when multiplied by 10,000 items. Shrink core scaling structs to their absolute minimum (e.g., 32 bytes) by sequentially packing strings into a memory arena and storing only pointers.
