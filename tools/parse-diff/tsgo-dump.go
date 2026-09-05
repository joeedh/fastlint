// Dumps tsgo's parse tree as an S-expression per file for the differential
// harness (node make.ts parse-diff). Built with `go build -overlay` so it
// compiles inside the TypeScript module and can import its internal packages
// without touching that checkout.
//
// stdin: one file path per line. stdout: for each file, `#file <path>` then
// `(Kind start end` / `)` lines, two-space indented, where start skips leading
// trivia and both are UTF-8 byte offsets. A file that cannot be read emits
// `#error <message>` after its header.
package main

import (
	"bufio"
	"fmt"
	"os"
	"path/filepath"
	"strings"

	"github.com/microsoft/TypeScript/tsc/internal/ast"
	"github.com/microsoft/TypeScript/tsc/internal/core"
	"github.com/microsoft/TypeScript/tsc/internal/parser"
	"github.com/microsoft/TypeScript/tsc/internal/scanner"
	"github.com/microsoft/TypeScript/tsc/internal/tspath"
)

func main() {
	in := bufio.NewScanner(os.Stdin)
	in.Buffer(make([]byte, 1<<20), 1<<20)
	out := bufio.NewWriterSize(os.Stdout, 1<<20)
	defer out.Flush()

	for in.Scan() {
		file := strings.TrimSpace(in.Text())
		if file == "" {
			continue
		}
		fmt.Fprintf(out, "#file %s\n", file)
		data, err := os.ReadFile(file)
		if err != nil {
			fmt.Fprintf(out, "#error %v\n", err)
			continue
		}
		abs, _ := filepath.Abs(file)
		abs = filepath.ToSlash(abs)
		text := string(data)
		opts := ast.SourceFileParseOptions{
			FileName: abs,
			Path:     tspath.ToPath(abs, "", true),
		}
		sf := parser.ParseSourceFile(opts, text, core.GetScriptKindFromFileName(abs))
		dump(out, text, sf.AsNode(), 0)
	}
}

func dump(out *bufio.Writer, text string, n *ast.Node, depth int) {
	start := scanner.SkipTrivia(text, n.Pos())
	fmt.Fprintf(out, "%*s(%s %d %d\n", depth*2, "", strings.TrimPrefix(n.Kind.String(), "Kind"), start, n.End())
	n.ForEachChild(func(c *ast.Node) bool {
		dump(out, text, c, depth+1)
		return false
	})
	fmt.Fprintf(out, "%*s)\n", depth*2, "")
}
