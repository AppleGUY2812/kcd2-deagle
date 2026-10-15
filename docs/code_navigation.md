# Code indexing for Claude: hype or real? (2026-10-15)

The user asked: "What about indexing the codebase? Do people do that in real life for Claude's performance, or is it
hype? What actually works?" Research by Claude 2 (a research agent read the sources below).

## Verdict
For a repo this size (~9k lines of Lua + C++, ~250 Lua functions, 15 script files), indexing is mostly hype. Measured
gains show up on repos with 1,000+ files or large SWE-bench projects, and even there plain grep stays close. Claude
Code itself dropped its vector index for agentic search (grep / glob / read). What does help here is cheap: a short,
correct map of which file owns which system (and who owns it), tests, and ast-grep for "every call to X".

## What was measured (numbers from the sources)
- Cursor (own benchmark, own embedding model): grep + semantic search answered questions 12.5% more accurately on
  average (6.5-23.5% by model). In a live A/B test: 0.3% more code kept, 2.6% on codebases of 1,000+ files. Grep and
  semantic search together worked best.
- Agentless (SWE-bench Lite): the right file found from a repo-structure prompt 78.7%, embeddings 67.7%, both 81.7%.
- SWE-agent: how search results are shown mattered more than having search: summarised search 18.0% solved,
  iterative 12.0%, no search tool 15.7%.
- Language server vs grep (Xu 2026, Claude models): when the task names the symbol, semantic retrieval cost 6-118%
  more tokens; the models chose grep 94-100% of the time (6 issues only).
- Structural index (Bhola 2026, 91 tasks): 50.4% solved vs 41.9% without, but vs an agentic-grep agent (45.3%) not
  significant (p = 0.087).
- A tree-sitter code graph over MCP: ~10x fewer tokens, answer quality 83% vs 92% for an agent reading files.
- AGENTS.md / context-file study: context files tend to lower task success and cost 20%+ more; file-by-file repo
  overviews didn't help. Anthropic's own best practices also say to leave file-by-file descriptions out of CLAUDE.md.

Claimed but not measured: Anthropic (Boris Cherny) says agentic search beat RAG + a vector database "by a lot", by
internal "vibes"; Sourcegraph says its keyword context fetching "works as well as embeddings"; Serena (LSP over MCP)
says it's more token-efficient; Aider's repo map has no benchmark.

## For this repo, ranked
1. Keep the ownership map in CLAUDE.md correct (who edits which file), plus a test that fails when a file it names
   no longer exists. ~20 min, almost no upkeep. Grep can't find ownership; a stale map misleads every session.
2. ast-grep (`sg`, installed): `sg -p 'self:SpawnFx($$$A)' -l lua` finds every call. Use rg for renames (strings,
   comments too).
3. Nothing else: `rg -n "function Deagle:X"` answers in milliseconds.
4. Optional: the official `lua-lsp` code-intelligence plugin (~15 min) for error reports after edits, not navigation.
   clangd for native/ needs a compile_commands.json (30-60 min). Serena: ~30 min, gains unmeasured. Skip.
5. Skip a function -> file:line index: it duplicates grep, and with 4 sessions editing, line numbers change every
   commit, so it would conflict and go stale constantly.

## Sources
- https://code.claude.com/docs/en/how-claude-code-works
- https://code.claude.com/docs/en/best-practices
- https://code.claude.com/docs/en/plugins/code-intelligence
- https://newsletter.pragmaticengineer.com/p/building-claude-code-with-boris-cherny
- https://officechai.com/ai/claude-researcher-explains-how-agentic-search-performed-better-than-rag-for-code-generation/
- https://cursor.com/blog/semsearch
- https://5.6.sourcegraph.com/cody/faq
- https://aider.chat/docs/repomap.html, https://aider.chat/2023/10/22/repomap.html
- https://arxiv.org/html/2405.15793 (SWE-agent), https://arxiv.org/html/2407.01489 (Agentless)
- https://arxiv.org/abs/2406.14497, https://arxiv.org/html/2608.13568, https://arxiv.org/html/2606.22417,
  https://arxiv.org/abs/2607.24882, https://arxiv.org/abs/2603.27277, https://arxiv.org/html/2602.11988v1
- https://github.com/oraios/serena, https://github.com/ast-grep/ast-grep-mcp
