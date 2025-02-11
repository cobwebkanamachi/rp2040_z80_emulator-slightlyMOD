// this code is shown here
// https://qiita.com/teddokano/items/9afc45bb1beab9b7af4f
#include <stdarg.h>
//  プロトタイプ宣言
void zprintf(const char *format, ...);
//  ここからが`zprintf`に関連する部分
#define MAX_ZPRINTF_LENGTH 80  // 一度に変換する最大文字数
void zprintf(const char *format, ...) {
  char s[MAX_ZPRINTF_LENGTH];
  va_list args;

  va_start(args, format);
  vsnprintf(s, MAX_ZPRINTF_LENGTH, format, args);
  va_end(args);

  Serial.print(s);
}
