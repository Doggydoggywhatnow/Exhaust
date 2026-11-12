---
title: Every element
tags: [sample, markdown]
aliases: [Sample note]
---

# Heading one #

Setext heading
==============

## Heading two

###### Heading six

A paragraph with *emphasis*, _underscores_, **strong**, ***both***, ~~strike~~, ==highlight==, `code span`, and an escaped \* star.
A second line with a hard break at the end  
and a third line. Inline math $a^2 + b^2 = c^2$ and display math $$x = y$$ in text.

Links: [[Wikilink]], [[Page|alias]], [[Page#Heading]], [[Page#^block]], ![[embed.png|100]], [text](https://example.com "Title"), ![alt](image.png), <https://auto.link>, https://bare.example/path?x=1#frag, and a footnote[^note] or inline^[footnote text].

Tags #tag and #nested/tag-two but not #123 or a#b. A block id follows. ^block-id

Comment %%hidden *text*%% inline and <b>html</b> <br> inline <!-- note -->.

> A quote
> continues here
> > nested quote

> [!note] A callout title
> Callout body with **strong**.
>
> - list in callout

> [!WARNING]- Closed and foldable
> Hidden by default.

> [!tldr]+ Open foldable alias
> Summary.

> [!custom-type] Unknown type
> Falls back to note.

- bullet one
- bullet two
  - nested bullet
    continued line
- [ ] open task
- [x] done task
- [?] other task

1. ordered one
2. ordered two
   1. nested ordered
3) different delimiter

```python title="x"
def f():
    return 1
```

~~~
tilde fence
~~~

    indented code
    more code

$$
\int_0^1 x\,dx
$$

%%
a multi-line
comment block
%%

| Left | Center | Right |
|:-----|:------:|------:|
| a | **b** | [[c\|alias]] |
| d | e |

---

[^note]: The footnote text,
    continued here.

<div class="box">
  <p>An HTML block</p>
</div>

Final paragraph with a URL https://example.org/end.
