#!/bin/bash
# Use this if intel_pstate isn't available
# It doesn't survive reboot

# Disable DVFS on all CPUs via MSR writes:
#   - Clear bit 16 of MSR_IA32_MISC_ENABLE (0x1A0) → disables EIST/SpeedStep
#   - Set bit 38 of MSR_IA32_MISC_ENABLE (0x1A0) → disables Turbo Boost

set -e

NCPUS=$(nproc --all)
echo "System has $NCPUS logical CPUs"

# Read current value from cpu0
CURRENT=$(sudo rdmsr -p 0 0x1a0)
echo "Current MSR_IA32_MISC_ENABLE (cpu0): 0x$CURRENT"

# Compute new value:
#   clear bit 16 (EIST enable): & ~0x10000
#   set   bit 38 (IDA engage / turbo disable): | 0x4000000000
NEW=$(printf '%x' $(( (16#$CURRENT & ~0x10000) | 0x4000000000 )))
echo "New value: 0x$NEW  (EIST off, Turbo off)"

echo "Writing to all $NCPUS CPUs..."
for cpu in $(seq 0 $((NCPUS - 1))); do
    sudo wrmsr -p $cpu 0x1a0 "0x$NEW"
done
echo "Done."

echo ""
echo "Verifying cpu0:"
sudo rdmsr -p 0 0x1a0
