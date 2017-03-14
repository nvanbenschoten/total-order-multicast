// +build !go1.7

package totalorder

import (
	"fmt"
	"testing"
)

// Subtest runs f as a subtest. It is meant to mock out the functionality of
// subtests that were added in Go 1.7.
func Subtest(t *testing.T, name string, f func(t *testing.T)) {
	fmt.Printf("=== RUN   /%s\n", name)
	f(t)
}
