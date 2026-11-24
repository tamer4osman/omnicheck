// SPRINT-10 resolver fixture: main imports six packages covering every
// resolver class -- two intra-repo (resolves, fan-out: "store" and the
// verbatim dotted path "example.org/app/store"), one aliased intra-repo
// (resolves, alias-independent), one module-prefix decoy (go.mod
// declares "mycorp", but the resolver never reads it, so "mycorp/store"
// stays unresolved), one stdlib and one remote path (outside the
// analyzed set; stay unresolved). Never guessed.
package main

import (
	"fmt"
	gs "example.org/app/store"
	st "util"

	"mycorp/store"
	"store"

	"github.com/x/remote"
)

func main() {
	fmt.Println(store.Name(), st.U(), gs.Greeting(), remote.X)
}
