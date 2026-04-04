#!/bin/bash
# level_sweep.sh — Interactive APRS TX level sweep
# Transmits at each level and asks if the radio decoded it.
# Usage: ./level_sweep.sh [options]
#   -f  flat audio, no pre-emphasis (default)
#   -e  enable pre-emphasis
#   -P  PTT device (default /dev/hidraw1)
#   -D  audio device (default hw:1,0)
#   -d  TX delay ms (default 1500)
#   -C  callsign (default WRDP51-9)

LIBPATH="/home/brian/libaprs/src/.libs"
BINDIR="/home/brian/libaprs/examples/.libs"

PREEMPH="-E"
HIDRAW="/dev/hidraw1"
AUDIO="hw:1,0"
DELAY=1500
CALL="WRDP51-9"

while getopts "feP:D:d:C:" opt; do
    case $opt in
        f) PREEMPH="-E" ;;
        e) PREEMPH="" ;;
        P) HIDRAW="$OPTARG" ;;
        D) AUDIO="$OPTARG" ;;
        d) DELAY="$OPTARG" ;;
        C) CALL="$OPTARG" ;;
    esac
done

TX() {
    local level="$1"
    local comment="$2"
    sudo LD_LIBRARY_PATH="$LIBPATH" "$BINDIR/aprs_tx" \
        -C "$CALL" $PREEMPH -l "$level" \
        -P "$HIDRAW" -D "$AUDIO" -d "$DELAY" \
        beacon -lat 35.21 -lon -97.50 \
        -comment "${comment:-lvl${level}}" 2>/dev/null
}

echo "========================================="
echo " APRS Level Sweep"
echo "========================================="
echo " Call:    $CALL"
echo " PTT:    $HIDRAW"
echo " Audio:  $AUDIO"
echo " Delay:  ${DELAY}ms"
echo " Pre-em: ${PREEMPH:-(on)}"
echo ""
echo " Commands at prompt:"
echo "   y/n    — decoded or not"
echo "   r      — repeat same level"
echo "   NUMBER — jump to that level (e.g. 0.07)"
echo "   e      — toggle pre-emphasis"
echo "   d NUM  — set TX delay"
echo "   q      — quit"
echo "========================================="
echo ""

LEVELS=(0.01 0.02 0.03 0.04 0.05 0.06 0.07 0.08 0.10 0.12 0.15 0.20 0.25 0.30 0.40 0.50)
RESULTS=()
IDX=0

while true; do
    if [ $IDX -ge ${#LEVELS[@]} ]; then
        echo ""
        echo "=== Sweep complete ==="
        break
    fi

    LVL="${LEVELS[$IDX]}"
    echo -n "TX level $LVL ... "
    TX "$LVL"
    echo "sent."

    while true; do
        read -p "  Decoded? [y/n/r/NUMBER/e/d NUM/q]: " ans
        case "$ans" in
            y|Y)
                echo "  >>> $LVL DECODED <<<"
                RESULTS+=("$LVL:YES")
                IDX=$((IDX + 1))
                break
                ;;
            n|N)
                RESULTS+=("$LVL:NO")
                IDX=$((IDX + 1))
                break
                ;;
            r|R)
                echo -n "  Repeating $LVL ... "
                TX "$LVL"
                echo "sent."
                ;;
            e|E)
                if [ -z "$PREEMPH" ]; then
                    PREEMPH="-E"
                    echo "  Pre-emphasis OFF (flat)"
                else
                    PREEMPH=""
                    echo "  Pre-emphasis ON"
                fi
                echo -n "  Repeating $LVL ... "
                TX "$LVL"
                echo "sent."
                ;;
            d\ *)
                DELAY="${ans#d }"
                echo "  TX delay set to ${DELAY}ms"
                echo -n "  Repeating $LVL ... "
                TX "$LVL"
                echo "sent."
                ;;
            q|Q)
                break 2
                ;;
            [0-9]*)
                LVL="$ans"
                LEVELS[$IDX]="$LVL"
                echo -n "  TX level $LVL ... "
                TX "$LVL"
                echo "sent."
                ;;
            *)
                echo "  y/n/r/NUMBER/e/d NUM/q"
                ;;
        esac
    done
done

echo ""
echo "========================================="
echo " Results"
echo "========================================="
for r in "${RESULTS[@]}"; do
    lvl="${r%%:*}"
    res="${r##*:}"
    if [ "$res" = "YES" ]; then
        printf "  %-8s  DECODED\n" "$lvl"
    else
        printf "  %-8s  no decode\n" "$lvl"
    fi
done
echo "========================================="
