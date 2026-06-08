---
name: facs-reference
description: On-demand reference for the Facial Action Coding System (FACS) as used in this project — Action Units, emotion mappings, and therapy exercise descriptions. Use when working on exercise data, FACS scoring, Blueprint logic, or WebUI exercise displays.
---

## What is FACS?

The **Facial Action Coding System (FACS)** is a scientific framework for describing facial movements using **Action Units (AUs)** — each AU corresponds to a specific facial muscle group.

This project uses AUs to:
- Define therapy exercises targeting specific facial muscles
- Score patient performance by detecting AU activation via the MetaHuman LiveLink face capture
- Provide guided instructions in the WebUI for each exercise

## Action Units Used in FaceMotion

| AU  | Name                  | Muscle                              | Movement |
|-----|-----------------------|-------------------------------------|----------|
| AU1 | Inner brow raiser     | Frontalis (medial)                  | Raise inner brows |
| AU2 | Outer brow raiser     | Frontalis (lateral)                 | Raise outer brows |
| AU4 | Brow lowerer          | Corrugator supercilii               | Pull brows down/together |
| AU5 | Upper lid raiser      | Levator palpebrae superioris        | Widen eyes |
| AU6 | Cheek raiser          | Orbicularis oculi                   | Raise cheeks |
| AU7 | Lid tightener         | Orbicularis oculi (squint)          | Squint eyes |
| AU9 | Nose wrinkler         | Nasalis / levator labii superioris  | Wrinkle nose / raise upper lip |
| AU12 | Lip corner puller    | Zygomaticus major                   | Smile (pull corners up) |
| AU15 | Lip corner depressor | Depressor anguli oris               | Pull mouth corners down |
| AU18 | Lip puckerer         | Orbicularis oris                    | Pout / pucker lips |
| AU20 | Lip stretcher        | Risorius                            | Stretch mouth horizontally |
| AU23 | Lip tightener        | Orbicularis oris (narrow)           | Narrow/tighten lips |
| AU24 | Lip pressor          | Orbicularis oris (press)            | Press lips together |
| AU26 | Jaw drop             | Mandible depression muscles         | Open mouth/jaw |

## Emotion Mappings (Exercise Targets)

| Emotion    | Required AUs               | Description |
|------------|----------------------------|-------------|
| Angry      | AU4 + AU7 + AU23 + AU24    | Knit brows together, press/clench lips |
| Surprised  | AU1 + AU2 + AU5 + AU26     | Open eyes wide, drop jaw |
| Disgust    | AU9 + AU15                 | Raise upper lip, draw corners down |
| Fear       | AU1 + AU2 + AU4 + AU5 + AU7 + AU20 | Wide brow raise, tense/wide eyes, stretched mouth |
| Smile      | AU12 + AU6                 | Pull lip corners up, raise cheeks |
| Sadness    | AU1 + AU4 + AU15           | Inner brow raise, corners drawn down |
| Pout       | AU18 + AU24                | Round lips forward, press gently |
| Puff cheeks | (cheeks)                  | Inflate cheeks, close lips, avoid jaw movement |
| Smile + Cheeks | AU12 + cheeks + (AU6) | Lip corner pull with cheek lift |

## How FACS Maps to the Codebase

- **MetaHuman LiveLink** streams AU values in real time from a face capture device (camera/iPhone) into the engine
- **Curve data** for each AU is logged by `CurveLogging.cpp` — each curve name corresponds to an AU blend shape on the MetaHuman face mesh
- **Exercise scoring** compares the patient's live AU activations against the target AU combination for a given exercise
- **Session modes:** `EFacialTherapySessionMode` (in `FacialTherapyApi.h`) distinguishes `GameMode` (gamified exercises) from `GuidedMode` (step-by-step guided exercises)

## Testing Procedure Notes (from project guide)

- Each user tests one phase (~30 min per phase)
- Patients should position the camera correctly and watch their own MetaHuman avatar for optimum score
- Exercises target specific muscle groups — instruct patients to move muscles, not just smile
- Demonstrating the exercise before asking the patient to perform it improves results
