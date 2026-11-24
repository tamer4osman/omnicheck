// A module whose root has no licence file: OC010 warns (SPRINT-11
// fixture). The dep below is NOT in the test OSV snapshot, so OC009
// stays silent for it.
module beta

go 1.21

require clean.example/ok v0.1.0
