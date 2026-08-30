# An owner reply shorter than the question is ambiguous — ask before writing it down

Lane L4b, 2026-08-30. The agent asked the owner two numbered questions. The
second contained three lettered options. The owner replied **`b:`**.

The agent read that as option (b) of the second question, and wrote "the owner
chose (b)" into `docs/HUMAN-QA-QUEUE.md` — the file that exists specifically to
stop agent inference from becoming owner instruction. The owner meant **item b**,
the second question itself, and rejected the entry in the same turn. It never
reached history.

The failure mode is usually described as something that happens across a relay:
one session telling another that the owner approved something. **It does not need
a relay.** One agent misreading one character of the owner's own message is
enough, and the agent then writes it down with complete confidence, because it
believes it was present for the decision.

**The rule: when the owner's answer is SHORTER than the question, ask before
writing.** A one-character reply to a multi-part question has more than one
reading, and the agent will reach for whichever is most convenient to act on.

**And record the owner's literal characters alongside the interpretation**, so a
later session can audit the interpretation instead of inheriting it. An entry
saying "the owner chose (b)" is unfalsifiable; one saying "the owner typed `b:`,
which this session read as ..." can be checked and corrected.
