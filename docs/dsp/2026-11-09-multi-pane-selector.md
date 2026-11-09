# Multi-pane selector: `+ PANE` / `- PANE` (HW-176, 2026-11-09)

Owner card HW-176, answer A (go ahead). Closes QA queue Z14 (the selector could
build only one pane) and Z6 (STORE read the first pane only). Written after the
code (CLAUDE.md "Before each phase" rule 6): every statement below was checked
against the shipped files.

## The problem

`selectPaneView` built exactly one pane per press. A workspace of two or three
panes (`kMaxPanes = 3`, `trace/Workspace.h`) existed only after OPEN restored a
saved session, so the multi-pane layout the session format and `WorkspaceView`
already support had no button that produced it (master plan row 9, "BUILT
(partial)").

## Options

**A. Shift-click a view button to add or remove.** Rejected.
- Invisible: nothing on screen says a modifier does anything.
- The selector's JUCE radio group lights exactly one button, so it would lie
  about a layout of several panes. Dropping the group is needed under any
  option, so this costs A nothing extra, but it does not fix the other points.
- Remove is ambiguous with duplicates: with two TRANSFER panes, shift-click on
  TRANSFER removes which one?
- `Button::triggerClick()` and a programmatic `onClick()` carry no
  `ModifierKeys`, so the behaviour could not be driven by the test seam every
  other selector test uses.

**B. Two explicit buttons (chosen).**
- `+ PANE` is a one-shot arm. The next view-button press appends that view
  (duplicates allowed: two TRANSFER panes on different sources is a real
  layout), then the arm clears. Pressing `+ PANE` twice disarms it.
- `- PANE` removes the last pane, the one most recently appended.
- A plain view press still replaces the workspace with one pane, so every
  existing habit and test keeps its meaning.
- `+ PANE` is disabled at `kMaxPanes`, `- PANE` at one pane. A refused edit that
  still reaches the code (for example a programmatic call) writes its reason to
  `paneReadout_`, the line under the selector row.

**C. A "layout" dropdown of presets.** Not pursued: three panes drawn from seven
views is 7 + 49 + 343 combinations; presets would either be arbitrary or
incomplete.

## Rules (pure, `view/PaneSelectorDecision.h`)

`decideViewPress`, `decideRemoveLast`, `canAddPane`, `canRemovePane`,
`storableKinds`, `viewsOf`. They have no JUCE dependency and are tested in
`app/tests/test_pane_selector_decision.cpp`, so the cap and the one-shot are
proven on all three CI systems. Add and remove re-split the workspace evenly
(weight 1.0 each): carrying the old weights over would give a new pane a share
nobody chose.

One behaviour change: pressing a view button while a multi-pane layout is on
screen now collapses it to that single view. The old code compared against the
first pane only and so ignored the press when the first pane already matched
(`[rta, transfer]` + RTA did nothing).

## Replace versus edit: what survives

Two kinds of change, deliberately different:

- **Replace** (a plain view press, session Open): `MainComponent::rebuildWorkspace(specs)`
  builds a whole new `WorkspaceView`. Every pane is a new component, so per-pane
  state resets (a TRANSFER pane's MAG/PHASE/COH source toggles go back to MTW,
  XOVER picks and topology are cleared). This is unchanged from before HW-176.
  Selector press and session Open used to carry two copies of the build,
  `setLibrary`, `syncTo` and `storeButton_` code, and each earlier fix (PR #45's
  unlit XOVER button, STORE task T6's enable state) kept missing the other copy;
  `rebuildWorkspace` is now the one place that REPLACES the workspace. (The
  constructor still builds the initial one-pane workspace in
  `MainComponent`'s init list; nothing else builds a `WorkspaceView`.)
- **Edit** (`+ PANE` then a view, `- PANE`): `WorkspaceView::appendPane(spec)`
  builds ONE new pane through the factory the workspace now keeps, and
  `removeLastPane()` destroys only the last. Every other pane is the same
  component object afterwards, so a TRANSFER pane flipped to FIXED stays on
  FIXED and an XOVER pane keeps its picks. An earlier cut of this lane routed
  edits through `rebuildWorkspace` and lost exactly that state (review finding,
  PR #105). Weights become equal after either.

`refreshPaneControls()` is the only place that derives the lit set, the two edit
buttons' enable state, `storeButton_` and the readout from the live workspace;
both paths end in it.

`currentPaneView_` still means the first pane; `storeClicked` and many tests read
it. Which panes are showing is `currentPaneViews()`, read off the live workspace.

## The lit set

Every view present is lit. The radio group is gone (`setRadioGroupId` removed),
since mutual exclusion would turn all but one off; `syncTo(set)` owns the toggle
state and a click never flips it (`setClickingTogglesState(false)`).

## STORE per kind (Z6)

STORE freezes one trace for each distinct storable kind present, RTA and
TRANSFER, in pane order.
- Two TRANSFER panes give one trace: both read the same fixed-FFT block, so a
  second trace would be a byte-identical duplicate in the library.
- The MTW clause is gated on "a TRANSFER trace was frozen", not on the first
  pane. The K8 scan over every TRANSFER pane is unchanged.
- `storeButton_` is enabled when any pane is RTA or TRANSFER. `[xover, transfer]`
  used to leave it disabled.
- The readout names every trace that landed and, when one kind could not be
  frozen (no reference fed), says `not stored: ...` for it, so a click that
  stored one trace is never read as two. A single-kind click keeps its old
  wording.

## Persistence

Session save already wrote every pane (`workspace_->paneSpecs()`), so there is
no schema change. `test_main_component_session_multipane.cpp` builds
RTA, TRANSFER, XOVER through the buttons, saves, opens and compares the specs.

## What this does not do

- It does not reorder panes, give a pane a custom weight, or remove a pane other
  than the last.
- A layout of 3 panes at the 1100 px minimum window gives each pane about a
  third of the height; nothing here changes that.
