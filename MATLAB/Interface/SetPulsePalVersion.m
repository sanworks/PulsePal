function SetPulsePalVersion
global PulsePalSystem
PulsePalSerialInterface('write', [PulsePalSystem.OpMenuByte 72], 'uint8');
pause(.1);
HandShakeOkByte = PulsePalSerialInterface('read', 1, 'uint8');
if HandShakeOkByte == 75
    PulsePalSystem.FirmwareVersion = PulsePalSerialInterface('read', 1, 'uint32'); % Get firmware version
    switch PulsePalSystem.FirmwareVersion
        case 2
            PulsePalSystem.CycleFrequency = round(20000); % Loops x 20k/sec
            PulsePalSystem.MinPulseDuration = round(100); % Minimum user settable pulse duration in microseconds
            PulsePalSystem.Bits = 8;
            PulsePalSystem.RegisterBits = 8;
        case 3
            PulsePalSystem.CycleFrequency = round(20000); % Loops x 20k/sec
            PulsePalSystem.MinPulseDuration = round(100); % Minimum user settable pulse duration in microseconds
            PulsePalSystem.Bits = 8;
            PulsePalSystem.RegisterBits = 8;
        case 4
            PulsePalSystem.CycleFrequency = round(20000); % Loops x 20k/sec
            PulsePalSystem.MinPulseDuration = round(100); % Minimum user settable pulse duration in microseconds
            PulsePalSystem.Bits = 8;
            PulsePalSystem.RegisterBits = 8;
        case 5
            PulsePalSystem.CycleFrequency = round(20000); % Loops x 20k/sec
            PulsePalSystem.MinPulseDuration = round(100); % Minimum user settable pulse duration in microseconds
            PulsePalSystem.Bits = 8;
            PulsePalSystem.RegisterBits = 8;
        case 20
            PulsePalSystem.CycleFrequency = round(20000); % Loops x 20k/sec
            PulsePalSystem.MinPulseDuration = round(100); % Minimum user settable pulse duration in microseconds
            PulsePalSystem.Bits = 12;
            PulsePalSystem.RegisterBits = 16;
            disp('NOTE: A firmware update is available. It fixes a bug in "Pulse Gated" trigger mode when used with multiple inputs.')
            disp('To update, follow the instructions at https://sites.google.com/site/pulsepalwiki/updating-firmware')
        case 21
            PulsePalSystem.CycleFrequency = round(20000); % Loops x 20k/sec
            PulsePalSystem.MinPulseDuration = round(100); % Minimum user settable pulse duration in microseconds
            PulsePalSystem.Bits = 12;
            PulsePalSystem.RegisterBits = 16;
    end
    PulsePalSystem.VoltageStep = 20/(2^PulsePalSystem.Bits);
else
    disp('Error: Pulse Pal returned an incorrect handshake signature.')
end