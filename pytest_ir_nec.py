import pytest
from pytest_embedded import Dut


@pytest.mark.esp32
@pytest.mark.smartroom

def test_smartroom_boot(dut: Dut) -> None:
    dut.expect_exact('SmartRoom start (target from sdkconfig: esp32c6)')
    dut.expect('Loaded .* learned signals')
    dut.expect_exact('Demodulating receiver note: learned timings are post-demodulation envelope only (carrier cannot be measured).')
