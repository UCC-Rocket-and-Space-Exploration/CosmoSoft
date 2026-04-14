# Project Management

**Date:** 01/04/2026  
**Review Period:** April 2026  
**Overall Focus:** Website development, CosmoSoft progress, blockers, and upcoming deadlines

---

## 1\. Project Portfolio Overview

### Active Projects

- Website  
- CosmoSoft

### Current Main Deadline

- **May 2026**

---

## 2\. Website

### Status

### ***In Progress***

### Current Priorities

| Priority | Task | Status | Notes |
| :---- | :---- | :---- | :---- |
| High \!\!\! | About Page | In Progress | Estimated remaining work: 7 |
| High \!\!\! | Sponsors Page | In Progress | Estimated remaining work: 3 |
| Medium \!\! | Contact Page | In Progress | Estimated remaining work: 3 |

### Future Development

| Priority | Task | Status | Notes |
| :---- | :---- | :---- | :---- |
| High \!\!\! | Projects Page | Planned | Needs structure and content |
| Medium \!\! | Launches Section | Planned | Sub-section under Projects |
| High \!\!\! | Admin Page | Planned | Requires design and access control planning |
| Low \! | News Section | Planned | Can be done after core pages |

### Next Actions

- Finish About Page  
- Finish Sponsors Page  
- Finish Contact Page  
- Define structure for Projects and Launches sections  
- Plan Admin Page requirements

---

## 3\. CosmoSoft

### Status

### **In Progress**

### Completed Work

- Base/framework for frontend  
- Port scanning for POSIX and Windows  
- Serial communication for POSIX

## Current Task Backlog

| Priority | Task | Status | Notes |
| :---- | :---- | :---- | :---- |
| Critical | General code review | To Do | Review code quality and Git structure |
| Critical | Proper testing framework | To Do | Includes unit, integration, box, and leak tests |
| Critical | Serialised data format decision | Blocked | Major blocker |
| High | Backend and frontend interoperability | To Do | Required before wider integration |
| High | Analyse AltOS data formats and logging | To Do | Needed for data handling |
| Medium | Serial communication for Windows | Blocked | Waiting on low-level resource management learning |
| Low | Flight replays | To Do | Can follow after core stability work |

## Testing Framework Breakdown

- [ ] Box tests  
- [ ] Memory leak tests  
- [ ] Unit tests  
- [ ] Integration tests

## Blockers

| Blocker | Impact | Action Required | Owner |
| :---- | :---- | :---- | :---- |
| Serialised data format undecided | Stops integration progress | Speak with Hardware-Avionics Team | You |
| Windows serial communication knowledge gap | Delays Windows support | Learn proper low-level resource management | You |

## Dependencies

- Hardware-Avionics Team input  
- Decision on serialised data format  
- Testing strategy definition

### Next Actions

- Perform general code review  
- Review Git structure and repository organisation  
- Speak with Hardware-Avionics Team about data format  
- Design and implement testing framework  
- Start backend/frontend interoperability work

---

## 4\. Priority Summary

### Critical

- Serialised data format decision  
- Hardware-Avionics discussion  
- General code review  
- Proper testing framework

### High

- Backend/frontend interoperability  
- Analyse AltOS data formats  
- Finish About and Sponsors pages

### Medium

- Contact page  
- Windows serial communication

### Low

- News section  
- Flight replays

---

## 5\. Weekly Planning Template

### This Week

- [ ] Complete About Page  
- [ ] Complete Sponsors Page  
- [ ] Review CosmoSoft Git structure  
- [ ] Speak with Hardware-Avionics Team  
- [ ] Draft testing framework plan

### Next Week

- [ ] Start backend/frontend integration  
- [ ] Continue AltOS format analysis  
- [ ] Work on Windows serial communication  
- [ ] Plan Admin Page structure

---

## 6\. Risks

| Risk | Effect | Mitigation |
| :---- | :---- | :---- |
| Undefined serialised data format | Integration blocked | Resolve with Hardware-Avionics Team quickly |
| Weak testing coverage | Hidden bugs and regressions | Build testing framework early |
| Too many parallel website tasks | Reduced focus | Finish current high-priority pages first |
| Windows implementation delay | Cross-platform support delayed | Prioritise low-level resource management learning |

---

## 7\. Notes

- Focus on removing blockers before adding new features.  
- Finish high-priority website pages before moving into future development items.  
- CosmoSoft needs stronger testing and integration discipline before feature expansion.

---

