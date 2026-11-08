#!/usr/bin/env python3
"""Idle fan stop with temperature guard; original thermal mode on exit."""
from pathlib import Path
import signal
import sys
import time

# Original DT curve: 16/32/48/64/80/96 at <50/50/55/60/65/70 C.
# Temporary development policy, not a validated production thermal curve.
LEVELS = (0, 128, 192, 255)
THRESHOLDS = (50000, 65000, 75000)
HYSTERESIS = 5000
stop = False


def choose_level(temp, previous):
    desired = sum(temp >= boundary for boundary in THRESHOLDS)
    if desired >= previous:
        return desired
    while previous > desired and temp <= THRESHOLDS[previous - 1] - HYSTERESIS:
        previous -= 1
    return previous


def find_fan():
    fans = [p for p in Path('/sys/class/hwmon').glob('hwmon*')
            if (p / 'name').read_text().strip() == 'pwmfan' and (p / 'temp_control').exists()]
    if len(fans) != 1:
        raise RuntimeError('Expected exactly one pwmfan with temp_control')
    return fans[0]


def restore():
    fan = find_fan()
    # Baseline observed before installation: automatic thermal control=1.
    (fan / 'temp_control').write_text('1')
    if (fan / 'temp_control').read_text().strip() != '1':
        raise RuntimeError('Could not restore original thermal control')
    print('Restored original kernel fan thermal control', flush=True)


def read_temperature(zones):
    if not zones:
        raise RuntimeError('No SoC temperature sensors')
    temperatures = [int((zone / 'temp').read_text()) for zone in zones]
    if any(t < -40000 or t > 150000 for t in temperatures):
        raise RuntimeError('Invalid temperature reading')
    return max(temperatures)


def request_stop(signum, frame):
    global stop
    stop = True


def main():
    signal.signal(signal.SIGTERM, request_stop)
    signal.signal(signal.SIGINT, request_stop)
    fan = find_fan()
    zones = [p for p in Path('/sys/class/thermal').glob('thermal_zone*')
             if (p / 'type').read_text().strip() in (
                 'soc-thermal', 'bigcore0-thermal', 'bigcore1-thermal',
                 'littlecore-thermal', 'center-thermal', 'gpu-thermal', 'npu-thermal')]
    # Validate temperature and enabled state before taking control.
    temp = read_temperature(zones)
    if int((fan / 'pwm1_enable').read_text()) == 0:
        raise RuntimeError('Fan is disabled; refusing to take control')
    level = choose_level(temp, 0)
    last_pwm = None
    try:
        (fan / 'temp_control').write_text('0')
        while not stop:
            try:
                temp = read_temperature(zones)
                level = choose_level(temp, level)
                pwm = LEVELS[level]
                if pwm != last_pwm or int((fan / 'pwm1').read_text()) != pwm:
                    (fan / 'pwm1').write_text(str(pwm))
                    if int((fan / 'pwm1').read_text()) != pwm:
                        raise RuntimeError('Fan PWM readback mismatch')
                    print(f'Fan PWM={pwm}/255 max_temperature={temp / 1000:.1f}C', flush=True)
                    last_pwm = pwm
            except Exception as error:
                # Keep cooling rather than stopping the fan on sensor failure.
                (fan / 'pwm1').write_text('255')
                print(f'Temperature/control error; requesting full fan: {error}', flush=True)
                last_pwm = None
            for _ in range(20):
                if stop:
                    break
                time.sleep(0.1)
    finally:
        restore()


def self_test():
    assert choose_level(41000, 0) == 0
    assert choose_level(50000, 0) == 1
    assert choose_level(49000, 1) == 1  # no threshold chatter
    assert choose_level(45000, 1) == 0
    assert choose_level(65000, 0) == 2
    assert choose_level(75000, 0) == 3  # immediate high-temperature response
    assert choose_level(74000, 3) == 3
    assert choose_level(70000, 3) == 2
    assert choose_level(40000, 3) == 0
    assert min(LEVELS) == 0 and max(LEVELS) == 255
    print('FAN_POLICY_TEST_OK')


if __name__ == '__main__':
    if sys.argv[1:] == ['--restore']:
        restore()
    elif sys.argv[1:] == ['--self-test']:
        self_test()
    elif not sys.argv[1:]:
        main()
    else:
        raise SystemExit('Usage: rk3588-fan-quiet.py [--restore|--self-test]')
