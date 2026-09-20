# Product hypothesis

DEOS is an experiment, not a finished product definition. The project deliberately keeps several possible end products open while fixing the underlying architectural thesis.

## The thesis

A modern physical device should be describable and controllable the way modern infrastructure is:

- declare what should exist;
- inspect what actually exists;
- preview changes;
- apply changes through typed controllers;
- detect drift;
- reproduce a device from configuration;
- let humans, automations and AI share the same control plane.

The product is therefore **not defined by the initial list of apps**. Early versions can be nearly empty and still test the key value proposition.

## Candidate product identities

We are intentionally evaluating several directions rather than prematurely choosing one:

### 1. Personal control plane
A physical console for a person's digital environment: devices, homelab, projects, home, services, files and models.

### 2. AI edge companion
A local interactive node that remains useful alone, but becomes much more capable when paired with Qwen or another local/remote model.

### 3. Operator cyberdeck
A technical console for USB/serial/network diagnostics, infrastructure state and programmable workflows.

### 4. Embedded product platform
A reusable base for small teams building dedicated touchscreen appliances without recreating settings, provisioning, updates, integrations and observability.

### 5. Distributed physical-computing fabric
Multiple DEOS nodes expose capabilities and state while external compute nodes provide storage, models and heavy processing.

No single identity is declared the winner yet. Real use on the P4 hardware should decide.

## What should feel different

### Infrastructure-as-device
The device is not configured by scattered imperative code paths. Display policy, storage policy, integrations, dashboards, AI providers and eventually apps are resources in one graph.

### AI without AI-dependency
The strongest AI use is not a chat screen. A model can understand intent, propose a plan, compose a dashboard or generate a deterministic automation. The resulting system then works without continued inference.

### Explainable state
The OS should be able to answer:

- What is desired?
- What is actually true?
- Why are they different?
- What will change if I apply this configuration?
- Which controller owns this resource?

### Reproducible physical devices
A user's setup should eventually be exportable as a manifest plus separately protected secrets/data and applied to another compatible node.

## Explicit non-goals for the first phases

- competing with Android/Linux as a general-purpose computer;
- supporting every ESP32 variant;
- implementing multiple language runtimes;
- an app store before the resource model proves useful;
- requiring Home Assistant;
- requiring cloud services;
- running large LLMs on the ESP32-P4 itself;
- automatic destructive storage operations;
- user-facing “safe mode” feature proliferation.

## Developer mode

Normal users get one operating mode. A hidden developer mode may expose:

- controller/resource inspector;
- desired vs actual state;
- event stream;
- heap/PSRAM/task/FPS metrics;
- I2C/GPIO/USB/SD/network diagnostics;
- unsigned package sideloading during development;
- verbose logs and remote debug endpoints.

Debug mode is a development surface, not a second personality for the product.

## How we judge viability

The architecture is promising only if real-hardware prototypes show that it provides clarity without unacceptable cost.

We will measure:

- core flash and RAM footprint;
- reconciliation latency;
- number of tasks and queues required;
- boot complexity;
- ease of adding a new controller;
- whether UI and CLI can truly share one state-change path;
- whether dependency handling remains understandable at 50–100 resources;
- whether developers can diagnose drift faster than with imperative firmware;
- whether AI-generated changes are safer and easier to inspect through `plan` than direct tool execution.

If these fail, the project should simplify rather than protect the architecture for ideological reasons.
