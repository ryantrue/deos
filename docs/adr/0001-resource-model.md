# ADR-0001: Resource-oriented control plane

Status: Accepted for prototype

## Context

The project needs one consistent way for UI, CLI, automations, remote management and AI to change durable device configuration. Traditional embedded firmware often exposes separate imperative paths that diverge over time.

## Decision

Durable configuration is represented as typed resources with stable identity, desired specification, dependencies and observed status. Controllers reconcile desired and actual state from an event-driven queue. One-shot operations remain Actions; observations remain State; occurrences remain Events.

## Consequences

Positive:

- deterministic plans;
- idempotent configuration;
- shared API for multiple front ends;
- observable drift;
- configuration export/reproduction;
- safer AI integration.

Costs:

- schema/versioning work;
- controller lifecycle complexity;
- memory used by the resource graph;
- temptation to model everything as a Resource.

The prototype must measure these costs on ESP32-P4 before the design is considered stable.
