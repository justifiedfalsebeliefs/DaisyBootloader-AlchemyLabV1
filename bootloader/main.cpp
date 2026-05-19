#include "bootloader.h"
#include "daisy_seed.h"
#include "alchemy_leds.h"

using namespace daisy;

DaisySeed hw;
Bootloader boot;

// Deinit callback for DaisySeed-based build. Stops audio and de-inits the hardware.
void DaisyDeInitCallback(void* context)
{
	DaisySeed* seed = reinterpret_cast<DaisySeed*>(context);
	if (seed)
	{
		seed->StopAudio();
		seed->DeInit();
	}
}

void AudioCallback(AudioHandle::InputBuffer  in,
				   AudioHandle::OutputBuffer out,
				   size_t                    size)
{
	for (size_t i = 0; i < size; i++)
	{
		out[0][i] = out[1][i] = 0;
	}

	boot.AudioProcess(in, out, size);
}

int main(void) {

	uint32_t timeout_ms = startup_process();

	hw.Configure();
	hw.Init(true);

	SCB_DisableDCache();

	boot.Init(hw.qspi, Pin(daisy::PORTC, 7), Pin(), timeout_ms, DaisyDeInitCallback, (void*)&hw);

	alchemy::LedInit();

	hw.StartAudio(AudioCallback);

	boot.IoInit();

	while(1)
	{
		boot.LoopProcess();
		alchemy::LedUpdate(System::GetNow());
	}
}
