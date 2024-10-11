// Wooting Analog SDK

using WootingAnalogResult = int;
using wooting_analog_initialise_t = WootingAnalogResult(*)();
using wooting_analog_uninitialise_t = WootingAnalogResult(*)();
using wooting_analog_set_keycode_mode_t = WootingAnalogResult(*)(int mode);
using wooting_analog_read_analog_t = float(*)(unsigned short code);
using wooting_analog_read_full_buffer_t = int(*)(unsigned short* code_buffer, float* analog_buffer, unsigned int len);

inline wooting_analog_read_analog_t wooting_analog_read_analog;
inline wooting_analog_read_full_buffer_t wooting_analog_read_full_buffer;

// Hooks

[[nodiscard]] extern float analogsense_transform_value(float value);
extern void analogsense_on_input_tick();
