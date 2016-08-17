function SetPulsePalVersion
global PulsePalSystem
ArCOM_PulsePal('write', PulsePalSystem.SerialPort, [PulsePalSystem.OpMenuByte 72], 'uint8');
pause(.1);
HandShakeOkByte = ArCOM_PulsePal('read', PulsePalSystem.SerialPort, 1, 'uint8');
if HandShakeOkByte == 75
    PulsePalSystem.FirmwareVersion = ArCOM_PulsePal('read', PulsePalSystem.SerialPort, 1, 'uint32'); % Get firmware version
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
        otherwise
            if PulsePalSystem.FirmwareVersion < 21
                ArCOM_PulsePal('write', PulsePalSystem.SerialPort, [PulsePalSystem.OpMenuByte 81], 'uint8');
                ArCOM_PulsePal('close', PulsePalSystem.SerialPort);
                error('Error: Pulse Pal 2 with old firmware detected. Please update your firmware. Update instructions are online at: https://sites.google.com/site/pulsepalwiki/updating-firmware');
            else
                PulsePalSystem.CycleFrequency = round(20000); % Loops x 20k/sec
                PulsePalSystem.MinPulseDuration = round(100); % Minimum user settable pulse duration in microseconds
                PulsePalSystem.Bits = 12;
                PulsePalSystem.RegisterBits = 16;
            end
    end
    PulsePalSystem.VoltageStep = 20/(2^PulsePalSystem.Bits);
else
    disp('Error: Pulse Pal returned an incorrect handshake signature.')
end