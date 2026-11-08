//! Deterministic native simulation primitives; not yet linked to Unreal.

pub const ABI_VERSION: u32 = 1;

#[no_mangle]
pub extern "C" fn marburgum_sim_core_abi_version() -> u32 {
    ABI_VERSION
}

/// Pure, architecture-independent district/day seed derivation.
#[no_mangle]
pub extern "C" fn marburgum_deterministic_seed(
    world_seed: u64,
    day_index: u32,
    district_id: u32,
) -> u64 {
    let mut z = world_seed ^ ((day_index as u64) << 32) ^ (district_id as u64);
    z = z.wrapping_add(0x9e3779b97f4a7c15);
    z = (z ^ (z >> 30)).wrapping_mul(0xbf58476d1ce4e5b9);
    z = (z ^ (z >> 27)).wrapping_mul(0x94d049bb133111eb);
    z ^ (z >> 31)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn abi_version() {
        assert_eq!(marburgum_sim_core_abi_version(), 1);
    }

    #[test]
    fn deterministic_replay() {
        assert_eq!(
            marburgum_deterministic_seed(42, 120, 8),
            marburgum_deterministic_seed(42, 120, 8)
        );
    }

    #[test]
    fn day_and_district_affect_seed() {
        let baseline = marburgum_deterministic_seed(42, 120, 8);
        assert_ne!(baseline, marburgum_deterministic_seed(42, 121, 8));
        assert_ne!(baseline, marburgum_deterministic_seed(42, 120, 9));
    }
}
