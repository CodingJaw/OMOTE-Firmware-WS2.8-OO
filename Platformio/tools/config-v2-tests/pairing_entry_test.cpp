#include "../../lib/esp32HalImpl/wifiHandler/config_v2_pairing_entry.hpp"
#include <cassert>
#include <iostream>
using namespace config_v2_pairing_entry;
int main() {
  PhysicalChord chord;
  assert(!chord.observe(Button::Home, Event::Long));
  assert(!chord.observe(Button::Home, Event::Press));
  assert(!chord.observe(Button::Home, Event::Long));
  assert(!chord.observe(Button::Back, Event::Press));
  assert(!chord.observe(Button::Other, Event::Long));
  assert(chord.observe(Button::Back, Event::Long));
  assert(!chord.observe(Button::Back, Event::Long)); // one shot
  assert(!chord.observe(Button::Home, Event::Long)); // no double trigger
  assert(!chord.observe(Button::Back, Event::Release));
  assert(!chord.observe(Button::Back, Event::Long));
  assert(!chord.observe(Button::Back, Event::Press));
  assert(chord.observe(Button::Home, Event::Long)); // rearmed
  chord.clear();
  assert(!chord.observe(Button::Back, Event::Long));
  std::cout << "config v2 local physical chord: PASS\n";
}
