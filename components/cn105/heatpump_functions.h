#pragma once
#include "globals.h"

#define MAX_FUNCTION_CODE_COUNT 30

struct HeatpumpFunctionCodes {
  bool valid[MAX_FUNCTION_CODE_COUNT] = {};
  int code[MAX_FUNCTION_CODE_COUNT] = {};
};

class HeatpumpFunctions {
 private:
  uint8_t raw[MAX_FUNCTION_CODE_COUNT];
  bool _isValid1;
  bool _isValid2;

  int get_code(uint8_t b);
  int get_value(uint8_t b);

 public:
  HeatpumpFunctions();

  bool is_valid() const;

  // data must be 15 bytes
  void set_data1(uint8_t *data);
  void set_data2(uint8_t *data);
  void get_data1(uint8_t *data) const;
  void get_data2(uint8_t *data) const;

  void clear();

  int get_value(int code);
  bool set_value(int code, int value);

  HeatpumpFunctionCodes get_all_codes();

  bool operator==(const HeatpumpFunctions &rhs);
  bool operator!=(const HeatpumpFunctions &rhs);
};
