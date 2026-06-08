---
name: webui-dev
description: Step-by-step workflow for editing patient-facing HTML/JS/CSS interfaces in Content/WebUI/ and understanding how they connect to Unreal Engine's WebBrowserWidget. Use when asked to modify any WebUI page, add a new UI screen, or debug JS-to-C++ communication.
---

## WebUI Architecture

The patient-facing interfaces live at:
```
source-code/Metahuman_Test/Content/WebUI/
  Auth/              # Login and signup pages
  CameraCheck/       # Camera setup validation
  ExerciseResult/    # Post-exercise result display
  GameExercise/      # Game-mode exercise UI
  GuidedExercise/    # Guided-mode exercise UI
  PatientDashboard/  # Main patient dashboard
  PhaseProgress/     # Phase completion view
  SessionCheckIn/    # Session start check-in
  UserDashboard/     # User overview
  Shared/            # Shared CSS (facemotion.css) and JS utilities
```

These HTML files are loaded inside the Unreal Engine **WebBrowserWidget** — a built-in UE component that renders a Chromium-based browser inside the game window.

## Editing a WebUI Page

1. Find the relevant HTML file under `Content/WebUI/<screen-name>/`.
2. Edit the HTML, CSS, or JS directly — these are plain web files. No build step needed.
3. The shared stylesheet is at `Content/WebUI/Shared/facemotion.css` — check it for existing classes before writing new styles.
4. **Important:** Because Content/ is gitignored, these files are not tracked in git. Always back up your work or move completed files out of the Content/ directory when committing.

## How JS Talks to C++ (WebBridge)

The C++ class `UFacialTherapyWebBridge` exposes functions to JavaScript via the `WebBrowserWidget` binding. When JavaScript calls a bound function, it executes C++ code.

To call a C++ function from JavaScript:
```js
// The bridge object is injected by UE — it's available as a global
window.ue.facialtherapywebbridge.someFunction(arg1, arg2);
```

To call JavaScript from C++, the `WebBrowserWidget` calls `ExecuteJavascript()`:
```cpp
WebBrowser->ExecuteJavascript(TEXT("myJsFunction(data)"));
```

When adding a new bridge function:
1. Declare it in `FacialTherapyWebBridge.h` with `UFUNCTION(BlueprintCallable)`
2. Implement it in `FacialTherapyWebBridge.cpp`
3. Recompile the project (Compile button in UE Editor, or Rider/VS)

## Adding a New Screen

1. Create a new folder under `Content/WebUI/<new-screen>/`.
2. Create an `index.html` (and `.css`, `.js` as needed).
3. In the UE Editor, open the Blueprint that should show this screen.
4. On the WebBrowserWidget, set the URL to the local file path of your new `index.html`. Local file paths in UE use the form `file:///Game/WebUI/<new-screen>/index.html`.

## Common Issues

- **Blank page:** Check the file path set on the WebBrowserWidget — it must match the Content/ directory path exactly.
- **JS not executing:** Open the UE Output Log (Window → Output Log) and look for browser console errors.
- **Changes not appearing:** UE may cache the web content. Restart the Play session or hot-reload the browser widget.
