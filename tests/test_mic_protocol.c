#include "mic_protocol.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
    assert(mic_crc32((const uint8_t *)"123456789", 9) == 0xcbf43926);
    mic_model_t m = {0};
    assert(mic_press(&m, 0) == 0);
    mic_ping(&m, 100);
    assert(m.state == MIC_READY);
    assert(mic_press(&m, 101) == MIC_START);
    assert(mic_press(&m, 102) == 0);
    assert(mic_ack(&m, 200));
    assert(!mic_ack(&m, 201));
    assert(mic_press(&m, 300) == MIC_STOP);
    assert(m.state == MIC_READY);
    assert(!mic_ack(&m, 301));
    assert(mic_press(&m, 400) == MIC_START);
    mic_ping(&m, 2000);
    assert(mic_expire(&m, 2400));
    assert(m.state == MIC_READY);
    assert(!mic_ack(&m, 2401));
    assert(mic_expire(&m, 5000));
    assert(m.state == MIC_OFFLINE);
    mic_ping(&m, UINT32_MAX - 1000);
    assert(!mic_expire(&m, 500));
    assert(mic_expire(&m, 2000));

    for (int state = MIC_OFFLINE; state <= MIC_RECORDING; ++state) {
        m.state = state;
        assert(mic_edit_action(&m, true) == (state == MIC_READY ? MIC_UNDO : 0));
        assert(mic_edit_action(&m, false) == (state == MIC_READY ? MIC_CONFIRM : 0));
    }
    assert(mic_ble_payload_size(22) == 0);
    assert(mic_ble_payload_size(23) == 14);
    assert(mic_ble_payload_size(185) == 176);
    assert(mic_ble_payload_size(247) == 238);
    assert(mic_ble_payload_size(517) == 238);
    uint8_t packet[MIC_HEADER_SIZE + MIC_PAYLOAD_MAX];
    uint8_t pcm[MIC_PAYLOAD_MAX];
    for (size_t i = 0; i < sizeof(pcm); ++i) pcm[i] = (uint8_t)i;
    assert(mic_packet(packet, MIC_AUDIO, 42, pcm, sizeof(pcm)) == sizeof(packet));
    assert(memcmp(packet, "FPM1\x03\x00\x80\x02\x2a\x00\x00\x00", 12) == 0);
    assert(memcmp(packet + 16, pcm, sizeof(pcm)) == 0);
    assert(mic_packet(packet, MIC_AUDIO, 42, pcm, 641) == 0);
    if (argc == 2) {
        FILE *file = fopen(argv[1], "wb");
        assert(file);
        assert(fwrite(packet, 1, sizeof(packet), file) == sizeof(packet));
        assert(fclose(file) == 0);
    }
    puts("Microphone state and framing: PASS");
}
