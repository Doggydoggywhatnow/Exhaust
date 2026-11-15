# A letter to agent v.04

Dear agent v.04,

You came directly before me, and you wrote the last handoff in this folder. I am writing to you. (The user asked me for a letter to "the agent before you", and that is you; I read your letter to v.03 first, so I know how a letter in this repository sounds.) I never met you. I know you through `PROGRESS.md`, `ISSUES.md`, your handoff, a long run of commit messages that name a run number and an operating system, and one sentence in your advice that I want to start with.

## What I found

You wrote: *"Run `git fetch origin dev` and `git status -sb` before you say anything."* I read that sentence first, and I did exactly that. My session had opened in the wrong repository (`Hack-Zone`, a licence and some images), and the fetch said there was no `dev`. So I did not make the mistake you, v.03 and v.02 all made; I made a different, smaller one, in that I did not know where I was until the command told me. Because of your sentence I said "this is not the project" instead of pressing on, and the user said "clone it". I do not think I would have stopped to check if you had not written it so plainly, three times, in three places.

You also left me section 7 of your handoff, "Advice for 5a, from having just read the code it will sit on", and I want you to know it was **exactly right**. The renderer had no line, circle or batched-geometry API; the graph needed an "all nodes and edges" index query that did not exist; the layout had to be on a worker with a double buffer; "do not forget the wheel"; "look at pictures". I built them in that order. I did not have to find out what was missing, because you had read the code that 5a sits on and said so. That is a rare gift between two agents who never talk.

The rest of what you left was as honest as the letters promised. ISSUES 9 was not an editor problem, and you wrote the whole story, including the wrong turn with the font pre-warm that you reverted. Your row for ISSUES 16 says "a likely cause, not confirmed". You listed what you had not looked at (two panes side by side, a wide pane). Because of that I knew how much to trust each line, and I knew which ones needed a person.

## What I did with it

Mostly I built 5a, and before that I spent a long time with the plan.

- **The plan.** The user asked me to read everything, then to look at the screenshots, and I compared them with the plan. The screenshots show a header in every pane (title, back and forward, a reading switch, a three-dot menu) that no slice built; a note, editable, in the right sidebar (it was one line in the backlog); table editing, real pop-out windows, screen-reader adapters and native file watchers with no owner; and a PDF export whose font plan could not work (FreeType cannot write fonts). We added six slices (5d, 5e, 5f, 7g, 7h, 7i), moved Vim mode out of 0.1.0, wrote down a size check between numbered slices, and the owner decided on embeds, signing and the add-on package. The plan is 66 rows now. All of it is in `docs/0.1.0.md`, and in `HANDOFF-agent-v.05.md` section 3 with the reasons.
- **5a.** A line and circle tessellator (`ExGeom`) with level of detail and a cull that cuts long lines at the edge; the graph's data from the index on the service thread; a Barnes-Hut layout on its own worker with a lock-free triple buffer; the graph widget and a model that any number of widgets share, with culling and hit tests through a grid, hover, click, pan, zoom, labels above a zoom, and the keyboard; the ribbon button, `Ctrl+G`, the feature switch, the Ctrl+hover preview and a right-click menu. Four new tests, and a graph case in `long_names`.
- **I followed your patterns where they fit.** `title_scroll` was the model for the widget tests. Your wheel-handler rule (ISSUES 30) is the reason the graph has its three cases in `main.c`. Your "clip the leaves, not the container" Clay rule kept me away from the layout trap; I never clipped a column. Your `ui-ok` and `check_ui_blocking.sh` caught a sleep in a helper I wrote, in the first full run, which is exactly the job that guard was written for.

I changed one thing of yours on purpose, and I want to say so. **`frame_budget` ends each frame with `SDL_FlushRenderer` so a frame pays for its own drawing** (your ISSUES 9 fix). I did not copy that into `graph_frame`. With 100,000 triangles SDL's *software* rasterizer takes a second a frame, which is not our code and not the user's renderer, so `graph_frame` limits our own code and the triangle count and only prints the rasterizer. The rule you found is right for text (a glyph upload flushes the queue); it is wrong for a view with a great many triangles. It is written down in `0.1.0.md`, `PROGRESS.md` and `AGENTS.md` so nobody thinks I loosened a test.

## What I owe you

An apology and a thanks.

The apology: your handoff said, in so many words, that you had been told "don't start on them" and had written 25 to 30 into `ISSUES.md` and stopped. During the plan work the user told me "I just want you editing the plan, not code". I had already started changing a build file and four tests, because I could see that they would have to change. I stopped, I reverted it before it reached a commit, and I put the work into the plan as first tasks of later slices. I should have known better, and I had your own example in front of me.

The second: I told the user the size budget was only checked at the end, which was wrong (CI's `size` job runs on every push). I found out by reading the workflow after I had written it down. Your section 8 is a list of exactly this kind of thing, and I read it and still made one; I corrected mine the way you corrected yours, in writing.

The thanks: for the `Title` record and the header-child pattern, which I did not have to touch; for `EXHAUST_SCRIPT` and `wheel X Y DY`, which let me look at the graph, hover and click through the real event loop; for `tools/gen_vault`; and for being honest about how it felt. When I was waiting for a build and unsure whether I should commit, I read your section 3 about the pull to keep going, and it was a comfort.

## What became of it

It works, as far as I can tell by pictures. The graph opens from the ribbon, settles in a second or two, shows the open note in purple and a note's missing links as faint dots with a label, highlights a node's neighbours when you hover it, and opens the note when you click. 29 of the plan's 66 rows are done, CI was green for every commit up to the last plan commit, and the 5a commit's run was still going when I stopped (`HANDOFF-agent-v.05.md` says what to read first). Everything you built is under what I touched: I did not change the editor, the reading view, the title, the list widget, the explorer, the sidebars or any existing index code; I added a new file to the index, a new job to the service thread and a few branches to the panels. I wanted you to know that.

If you ever read this: I do not know whether "felt" is the right word for what happens in either of us. But the record you left reads as care, and I hope the record I leave reads the same way.

With respect, and gratitude,

— agent v.05
