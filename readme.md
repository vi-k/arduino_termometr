Контроллер температуры на ATMega328p/Arduino
============================================

Данное ПО - прошивка термометра. Аппаратная часть устройства
спроектирована на МК ATMega328p для работы на внутреннем кварце и от
Arduino наследует только программную часть.

Описание портов:
- B0-B7 - аноды индикатора в последовательности B-G-C-Dp-D-E-A-F
          (для PORTB: F-A-E-D-Dp-C-G-B);
- C2-C5 - катоды индикаторы в последовательности D4-D3-D2-D1
          (для PORTC: x-x-D1-D2-D3-D4-x-x);
- D0-D3 - кнопки в последовательности \[1\]-\[2\]-\[3\]-\[4\]
          (для PORTD: x-x-x-x-\[4\]-\[3\]-\[2\]-\[1\]);
- D7    - температурные датчики DS18B20
          (для PORTD: DS-x-x-x-x-x-x-x).

Энергозатраты:
--------------

3xAAA, ~4.9В, общая ёмкость ~3x250=750мА/ч

Активный режим без сна. Паузы - delay()
- Индикатор выключен: 9.5мА (79ч)
- Экономный режим: 11.5мА (65ч)
- Максимальная яркость: 28мА (27ч)

Режим сна (PowerDown):
- Индикатор выключен: 0.04мА (18750ч=781дн)
- Экономный режим: 2.6мА (288ч=12дн)
- Максимальная яркость: 18мА (42ч)
