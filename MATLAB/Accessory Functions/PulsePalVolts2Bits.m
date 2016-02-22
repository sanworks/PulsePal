function Bits = PulsePalVolts2Bits(Voltage, RegisterBits)
Bits = ceil(((Voltage+10)/20)*(2^(RegisterBits)-1));