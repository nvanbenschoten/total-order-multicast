// +build go1.7

package totalorder

import "testing"

// Subtest runs f as a subtest. This is a wrapper around T.Run, which was
// introduced in Go 1.7.
func Subtest(t *testing.T, name string, f func(t *testing.T)) {
	t.Run(name, f)
}
