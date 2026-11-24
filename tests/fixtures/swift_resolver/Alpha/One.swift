// Resolver corpus B (SPRINT-23): the import cycle, side one -- the
// Alpha target claims module Beta; OC003 pins the cycle (the analyzer
// reports the declared facts: SPM would refuse this at build time, the
// scan of sources does not).
import Beta

class One {
    let pair = 1
}
