function gui(obj)
% Create figure
obj.ui.Figure = uifigure('Visible', 'off');
obj.ui.Figure.Position = [100 100 725 480];
obj.ui.Figure.Name = 'Pulse Pal Parameter GUI';

% Create Toolbar
obj.ui.Toolbar = uitoolbar(obj.ui.Figure);

% Create PushTool_RestoreParams
obj.ui.PushTool_RestoreParams = uipushtool(obj.ui.Toolbar);
obj.ui.PushTool_RestoreParams.Tooltip = {'Restore Defaults'};
obj.ui.PushTool_RestoreParams.Icon = fullfile(matlabroot,'toolbox','matlab','icons','file_new.png');

% Create PushTool_LoadProgram
obj.ui.PushTool_LoadProgram = uipushtool(obj.ui.Toolbar);
obj.ui.PushTool_LoadProgram.Tooltip = {'Load Program'};
obj.ui.PushTool_LoadProgram.Icon = fullfile(matlabroot,'toolbox','matlab','icons','file_open.png');

% Create PushTool_SaveProgram
obj.ui.PushTool_SaveProgram = uipushtool(obj.ui.Toolbar);
obj.ui.PushTool_SaveProgram.Tooltip = {'Save Program'};
obj.ui.PushTool_SaveProgram.Icon = fullfile(matlabroot,'toolbox','matlab','icons','file_save.png');

% Create PushTool_UploadProgram
obj.ui.PushTool_UploadProgram = uipushtool(obj.ui.Toolbar);
obj.ui.PushTool_UploadProgram.Tooltip = {'Load program to device'};
obj.ui.PushTool_UploadProgram.Icon = fullfile(matlabroot,'toolbox','matlab','icons','linkproduct.png');

% Create OutputChannelsPanel
obj.ui.OutputChannelsPanel = uipanel(obj.ui.Figure);
obj.ui.OutputChannelsPanel.Title = 'Output Channels';
obj.ui.OutputChannelsPanel.FontWeight = 'bold';
obj.ui.OutputChannelsPanel.Position = [13 251 700 164];

% Create PulseTypeLabel
obj.ui.PulseTypeLabel = uilabel(obj.ui.OutputChannelsPanel);
obj.ui.PulseTypeLabel.Position = [164 111 64 22];
obj.ui.PulseTypeLabel.Text = 'Pulse Type';

% Create DropDown_PulseType
obj.ui.DropDown_PulseType = uidropdown(obj.ui.OutputChannelsPanel);
obj.ui.DropDown_PulseType.Items = {'Monophasic', 'Biphasic'};
obj.ui.DropDown_PulseType.Tooltip = {'Biphasic pulses add an interval at the resting voltage and then a second phase to each pulse'};
obj.ui.DropDown_PulseType.Position = [145 84 102 22];
obj.ui.DropDown_PulseType.Value = 'Monophasic';

% Create CustomTrainIDLabel_2
obj.ui.CustomTrainIDLabel_2 = uilabel(obj.ui.OutputChannelsPanel);
obj.ui.CustomTrainIDLabel_2.Position = [455 111 92 22];
obj.ui.CustomTrainIDLabel_2.Text = 'Custom Train ID';

% Create DropDown_CustomTrainID
obj.ui.DropDown_CustomTrainID = uidropdown(obj.ui.OutputChannelsPanel);
obj.ui.DropDown_CustomTrainID.Items = {'0 (None)', '1', '2', '3', '4'};
obj.ui.DropDown_CustomTrainID.Position = [461 84 82 22];
obj.ui.DropDown_CustomTrainID.Value = '0 (None)';

% Create CustomTrainofLabel
obj.ui.CustomTrainofLabel = uilabel(obj.ui.OutputChannelsPanel);
obj.ui.CustomTrainofLabel.Position = [555 111 90 22];
obj.ui.CustomTrainofLabel.Text = 'Custom Train of';

% Create DropDown_CustomTrainTarget
obj.ui.DropDown_CustomTrainTarget = uidropdown(obj.ui.OutputChannelsPanel);
obj.ui.DropDown_CustomTrainTarget.Items = {'Pulses', 'Bursts'};
obj.ui.DropDown_CustomTrainTarget.Enable = 'off';
obj.ui.DropDown_CustomTrainTarget.Tooltip = {'Custom train timestamps can indicate the onset of either each pulse, or each burst of pulses'};
obj.ui.DropDown_CustomTrainTarget.Position = [560 84 82 22];
obj.ui.DropDown_CustomTrainTarget.Value = 'Pulses';

% Create LoopLabel
obj.ui.LoopLabel = uilabel(obj.ui.OutputChannelsPanel);
obj.ui.LoopLabel.Position = [657 111 32 22];
obj.ui.LoopLabel.Text = 'Loop';

% Create CheckBox_CustomTrainLoop
obj.ui.CheckBox_CustomTrainLoop = uicheckbox(obj.ui.OutputChannelsPanel);
obj.ui.CheckBox_CustomTrainLoop.Tooltip = {'If enabled, custom pulse train will loop until the pulse train duration (Train (s) below)'};
obj.ui.CheckBox_CustomTrainLoop.Enable = 'off';
obj.ui.CheckBox_CustomTrainLoop.Text = '';
obj.ui.CheckBox_CustomTrainLoop.Position = [665 84 17 22];

% Create Phase1VoltsLabel
obj.ui.Phase1VoltsLabel = uilabel(obj.ui.OutputChannelsPanel);
obj.ui.Phase1VoltsLabel.Position = [262 111 83 22];
obj.ui.Phase1VoltsLabel.Text = 'Phase1 (Volts)';

% Create Phase2VoltsLabel
obj.ui.Phase2VoltsLabel = uilabel(obj.ui.OutputChannelsPanel);
obj.ui.Phase2VoltsLabel.Position = [362 111 83 22];
obj.ui.Phase2VoltsLabel.Text = 'Phase2 (Volts)';

% Create EditField_Phase1Voltage
obj.ui.EditField_Phase1Voltage = uieditfield(obj.ui.OutputChannelsPanel, 'numeric');
obj.ui.EditField_Phase1Voltage.HorizontalAlignment = 'center';
obj.ui.EditField_Phase1Voltage.Tooltip = {'Voltage of the first phase of each pulse (s)'};
obj.ui.EditField_Phase1Voltage.Position = [259 84 85 22];

% Create EditField_Phase2Voltage
obj.ui.EditField_Phase2Voltage = uieditfield(obj.ui.OutputChannelsPanel, 'numeric');
obj.ui.EditField_Phase2Voltage.HorizontalAlignment = 'center';
obj.ui.EditField_Phase2Voltage.Enable = 'off';
obj.ui.EditField_Phase2Voltage.Tooltip = {'Voltage of the second phase of each pulse (s)'};
obj.ui.EditField_Phase2Voltage.Position = [360 84 85 22];

% Create Phase1sLabel
obj.ui.Phase1sLabel = uilabel(obj.ui.OutputChannelsPanel);
obj.ui.Phase1sLabel.Position = [19 49 63 22];
obj.ui.Phase1sLabel.Text = 'Phase1 (s)';

% Create EditField_Phase1Duration
obj.ui.EditField_Phase1Duration = uieditfield(obj.ui.OutputChannelsPanel, 'numeric');
obj.ui.EditField_Phase1Duration.HorizontalAlignment = 'center';
obj.ui.EditField_Phase1Duration.Tooltip = {'Duration of the first phase of each pulse (s)'};
obj.ui.EditField_Phase1Duration.Position = [14 20 72 22];

% Create EditField_InterPhaseInterval
obj.ui.EditField_InterPhaseInterval = uieditfield(obj.ui.OutputChannelsPanel, 'numeric');
obj.ui.EditField_InterPhaseInterval.HorizontalAlignment = 'center';
obj.ui.EditField_InterPhaseInterval.Enable = 'off';
obj.ui.EditField_InterPhaseInterval.Tooltip = {'Interval between pulse phases (s)'};
obj.ui.EditField_InterPhaseInterval.Position = [101 20 72 22];

% Create EditField_Phase2Duration
obj.ui.EditField_Phase2Duration = uieditfield(obj.ui.OutputChannelsPanel, 'numeric');
obj.ui.EditField_Phase2Duration.HorizontalAlignment = 'center';
obj.ui.EditField_Phase2Duration.Enable = 'off';
obj.ui.EditField_Phase2Duration.Tooltip = {'Duration of the second phase of each pulse (s)'};
obj.ui.EditField_Phase2Duration.Position = [188 20 72 22];

% Create EditField_InterPulseInterval
obj.ui.EditField_InterPulseInterval = uieditfield(obj.ui.OutputChannelsPanel, 'numeric');
obj.ui.EditField_InterPulseInterval.HorizontalAlignment = 'center';
obj.ui.EditField_InterPulseInterval.Tooltip = {'Interval between pulse-end and the next pulse (s)'};
obj.ui.EditField_InterPulseInterval.Position = [276 20 72 22];

% Create EditField_BurstDuration
obj.ui.EditField_BurstDuration = uieditfield(obj.ui.OutputChannelsPanel, 'numeric');
obj.ui.EditField_BurstDuration.HorizontalAlignment = 'center';
obj.ui.EditField_BurstDuration.Tooltip = {'Duration of pulse bursts (0 = no bursts, units = seconds)'};
obj.ui.EditField_BurstDuration.Position = [360 20 72 22];

% Create EditField_InterBurstInterval
obj.ui.EditField_InterBurstInterval = uieditfield(obj.ui.OutputChannelsPanel, 'numeric');
obj.ui.EditField_InterBurstInterval.HorizontalAlignment = 'center';
obj.ui.EditField_InterBurstInterval.Tooltip = {'Interval betwen pulse bursts (s)'};
obj.ui.EditField_InterBurstInterval.Position = [445 20 72 22];

% Create EditField_PulseTrainDuration
obj.ui.EditField_PulseTrainDuration = uieditfield(obj.ui.OutputChannelsPanel, 'numeric');
obj.ui.EditField_PulseTrainDuration.HorizontalAlignment = 'center';
obj.ui.EditField_PulseTrainDuration.Tooltip = {'Duration of the pulse train (s)'};
obj.ui.EditField_PulseTrainDuration.Position = [528 20 72 22];

% Create EditField_PulseTrainDelay
obj.ui.EditField_PulseTrainDelay = uieditfield(obj.ui.OutputChannelsPanel, 'numeric');
obj.ui.EditField_PulseTrainDelay.HorizontalAlignment = 'center';
obj.ui.EditField_PulseTrainDelay.Tooltip = {'Delay from trigger to pulse train onset (s)'};
obj.ui.EditField_PulseTrainDelay.Position = [613 20 72 22];

% Create ChannelButtonGroup_OutputChan
obj.ui.ChannelButtonGroup_OutputChan = uibuttongroup(obj.ui.OutputChannelsPanel);
obj.ui.ChannelButtonGroup_OutputChan.Tooltip = {'Select an output channel to edit'};
obj.ui.ChannelButtonGroup_OutputChan.BorderType = 'none';
obj.ui.ChannelButtonGroup_OutputChan.Title = 'Channel';
obj.ui.ChannelButtonGroup_OutputChan.Position = [12 83 123 48];

% Create RadioButton_OutputCh1
obj.ui.RadioButton_OutputCh1 = uiradiobutton(obj.ui.ChannelButtonGroup_OutputChan,'Interpreter','html');
obj.ui.RadioButton_OutputCh1.Text = '1';
obj.ui.RadioButton_OutputCh1.Position = [4 4 25 22];
obj.ui.RadioButton_OutputCh1.Value = true;

% Create RadioButton_OutputCh2
obj.ui.RadioButton_OutputCh2 = uiradiobutton(obj.ui.ChannelButtonGroup_OutputChan,'Interpreter','html');
obj.ui.RadioButton_OutputCh2.Text = '2';
obj.ui.RadioButton_OutputCh2.Position = [33 4 33 22];

% Create RadioButton_OutputCh3
obj.ui.RadioButton_OutputCh3 = uiradiobutton(obj.ui.ChannelButtonGroup_OutputChan,'Interpreter','html');
obj.ui.RadioButton_OutputCh3.Text = '3';
obj.ui.RadioButton_OutputCh3.Position = [66 4 25 22];

% Create RadioButton_OutputCh4
obj.ui.RadioButton_OutputCh4 = uiradiobutton(obj.ui.ChannelButtonGroup_OutputChan,'Interpreter','html');
obj.ui.RadioButton_OutputCh4.Text = '4';
obj.ui.RadioButton_OutputCh4.Position = [96 4 29 22];

% Create PhaseIntervalLabel
obj.ui.PhaseIntervalLabel = uilabel(obj.ui.OutputChannelsPanel);
obj.ui.PhaseIntervalLabel.Position = [98 49 82 22];
obj.ui.PhaseIntervalLabel.Text = 'Phase Interval';

% Create Phase2sLabel
obj.ui.Phase2sLabel = uilabel(obj.ui.OutputChannelsPanel);
obj.ui.Phase2sLabel.Position = [194 49 63 22];
obj.ui.Phase2sLabel.Text = 'Phase2 (s)';

% Create PulseIntervalLabel
obj.ui.PulseIntervalLabel = uilabel(obj.ui.OutputChannelsPanel);
obj.ui.PulseIntervalLabel.Position = [276 49 78 22];
obj.ui.PulseIntervalLabel.Text = 'Pulse Interval';

% Create BurstsLabel
obj.ui.BurstsLabel = uilabel(obj.ui.OutputChannelsPanel);
obj.ui.BurstsLabel.Position = [371 49 50 22];
obj.ui.BurstsLabel.Text = 'Burst (s)';

% Create BurstIntervalLabel
obj.ui.BurstIntervalLabel = uilabel(obj.ui.OutputChannelsPanel);
obj.ui.BurstIntervalLabel.Position = [444 49 76 22];
obj.ui.BurstIntervalLabel.Text = 'Burst Interval';

% Create TrainsLabel
obj.ui.TrainsLabel = uilabel(obj.ui.OutputChannelsPanel);
obj.ui.TrainsLabel.Position = [540 49 49 22];
obj.ui.TrainsLabel.Text = 'Train (s)';

% Create TrainDelayLabel
obj.ui.TrainDelayLabel = uilabel(obj.ui.OutputChannelsPanel);
obj.ui.TrainDelayLabel.Position = [617 49 66 22];
obj.ui.TrainDelayLabel.Text = 'Train Delay';

% Create TriggerChannelsPanel
obj.ui.TriggerChannelsPanel = uipanel(obj.ui.Figure);
obj.ui.TriggerChannelsPanel.Title = 'Trigger Channels';
obj.ui.TriggerChannelsPanel.FontWeight = 'bold';
obj.ui.TriggerChannelsPanel.Position = [13 156 701 79];

% Create ChannelButtonGroup_TriggerChan
obj.ui.ChannelButtonGroup_TriggerChan = uibuttongroup(obj.ui.TriggerChannelsPanel);
obj.ui.ChannelButtonGroup_TriggerChan.Tooltip = {'Select a trigger channel to edit'};
obj.ui.ChannelButtonGroup_TriggerChan.BorderType = 'none';
obj.ui.ChannelButtonGroup_TriggerChan.Title = 'Channel';
obj.ui.ChannelButtonGroup_TriggerChan.Position = [10 5 60 48];

% Create RadioButton_TriggerCh1
obj.ui.RadioButton_TriggerCh1 = uiradiobutton(obj.ui.ChannelButtonGroup_TriggerChan,'Interpreter','html');
obj.ui.RadioButton_TriggerCh1.Text = '1';
obj.ui.RadioButton_TriggerCh1.Position = [5 4 25 22];
obj.ui.RadioButton_TriggerCh1.Value = true;

% Create RadioButton_TriggerCh2
obj.ui.RadioButton_TriggerCh2 = uiradiobutton(obj.ui.ChannelButtonGroup_TriggerChan,'Interpreter','html');
obj.ui.RadioButton_TriggerCh2.Text = '2';
obj.ui.RadioButton_TriggerCh2.Position = [33 4 33 22];

% Create LinktooutputsLabel
obj.ui.LinktooutputsLabel = uilabel(obj.ui.TriggerChannelsPanel);
obj.ui.LinktooutputsLabel.Position = [222 33 83 22];
obj.ui.LinktooutputsLabel.Text = 'Link to outputs';

% Create TriggerModeLabel
obj.ui.TriggerModeLabel = uilabel(obj.ui.TriggerChannelsPanel);
obj.ui.TriggerModeLabel.Position = [107 33 76 22];
obj.ui.TriggerModeLabel.Text = 'Trigger Mode';

% Create DropDown_TriggerMode
obj.ui.DropDown_TriggerMode = uidropdown(obj.ui.TriggerChannelsPanel);
obj.ui.DropDown_TriggerMode.Items = {'Normal', 'Toggle', 'Pulse Gated'};
obj.ui.DropDown_TriggerMode.Tooltip = {'Normal: TTL during pulse train ignored. Toggle: TTL during pulse train stops train. Pulse Gated: Pulse train only runs while trigger is high'};
obj.ui.DropDown_TriggerMode.Position = [96 8 100 22];
obj.ui.DropDown_TriggerMode.Value = 'Normal';

% Create CheckBox_LinkToOutputCh1
obj.ui.CheckBox_LinkToOutputCh1 = uicheckbox(obj.ui.TriggerChannelsPanel);
obj.ui.CheckBox_LinkToOutputCh1.Tooltip = {'Link trigger channel to output channel 1'};
obj.ui.CheckBox_LinkToOutputCh1.Text = 'Ch1';
obj.ui.CheckBox_LinkToOutputCh1.Position = [223 9 44 22];

% Create CheckBox_LinkToOutputCh2
obj.ui.CheckBox_LinkToOutputCh2 = uicheckbox(obj.ui.TriggerChannelsPanel);
obj.ui.CheckBox_LinkToOutputCh2.Tooltip = {'Link trigger channel to output channel 2'};
obj.ui.CheckBox_LinkToOutputCh2.Text = 'Ch2';
obj.ui.CheckBox_LinkToOutputCh2.Position = [283 9 44 22];

% Create CheckBox_LinkToOutputCh3
obj.ui.CheckBox_LinkToOutputCh3 = uicheckbox(obj.ui.TriggerChannelsPanel);
obj.ui.CheckBox_LinkToOutputCh3.Tooltip = {'Link trigger channel to output channel 3'};
obj.ui.CheckBox_LinkToOutputCh3.Text = 'Ch3';
obj.ui.CheckBox_LinkToOutputCh3.Position = [340 9 44 22];

% Create CheckBox_LinkToOutputCh4
obj.ui.CheckBox_LinkToOutputCh4 = uicheckbox(obj.ui.TriggerChannelsPanel);
obj.ui.CheckBox_LinkToOutputCh4.Tooltip = {'Link trigger channel to output channel 4'};
obj.ui.CheckBox_LinkToOutputCh4.Text = 'Ch4';
obj.ui.CheckBox_LinkToOutputCh4.Position = [397 9 44 22];

% Create CustomPulseTrainsPanel
obj.ui.CustomPulseTrainsPanel = uipanel(obj.ui.Figure);
obj.ui.CustomPulseTrainsPanel.Title = 'Custom Pulse Trains';
obj.ui.CustomPulseTrainsPanel.FontWeight = 'bold';
obj.ui.CustomPulseTrainsPanel.Position = [13 24 700 115];

% Create ListBox_CustomTrainID
obj.ui.ListBox_CustomTrainID = uilistbox(obj.ui.CustomPulseTrainsPanel);
obj.ui.ListBox_CustomTrainID.Items = {'1', '2', '3', '4'};
obj.ui.ListBox_CustomTrainID.Enable = 'off';
obj.ui.ListBox_CustomTrainID.Tooltip = {'Select the custom train to program'};
obj.ui.ListBox_CustomTrainID.Position = [15 22 100 37];
obj.ui.ListBox_CustomTrainID.Value = '1';

% Create CustomTrainIDLabel
obj.ui.CustomTrainIDLabel = uilabel(obj.ui.CustomPulseTrainsPanel);
obj.ui.CustomTrainIDLabel.Position = [15 64 92 22];
obj.ui.CustomTrainIDLabel.Text = 'Custom Train ID';

% Create TextArea_CustomTrainTimestamps
obj.ui.TextArea_CustomTrainTimestamps = uitextarea(obj.ui.CustomPulseTrainsPanel);
obj.ui.TextArea_CustomTrainTimestamps.Enable = 'off';
obj.ui.TextArea_CustomTrainTimestamps.Tooltip = {'Enter the timestamp of each pulse in the custom pulse train (comma delimited, units = seconds)'};
obj.ui.TextArea_CustomTrainTimestamps.Position = [133 13 271 47];

% Create TimestampssLabel
obj.ui.TimestampssLabel = uilabel(obj.ui.CustomPulseTrainsPanel);
obj.ui.TimestampssLabel.Position = [133 64 87 22];
obj.ui.TimestampssLabel.Text = 'Timestamps (s)';

% Create TextArea_CustomTrainVoltages
obj.ui.TextArea_CustomTrainVoltages = uitextarea(obj.ui.CustomPulseTrainsPanel);
obj.ui.TextArea_CustomTrainVoltages.Enable = 'off';
obj.ui.TextArea_CustomTrainVoltages.Tooltip = {'Enter the voltage of each pulse in the custom pulse train (comma delimited, units = volts)'};
obj.ui.TextArea_CustomTrainVoltages.Position = [416 13 271 47];

% Create VoltagesVLabel
obj.ui.VoltagesVLabel = uilabel(obj.ui.CustomPulseTrainsPanel);
obj.ui.VoltagesVLabel.Position = [417 64 70 22];
obj.ui.VoltagesVLabel.Text = 'Voltages (V)';

% Create PulsePalProgramEditorLabel
obj.ui.PulsePalProgramEditorLabel = uilabel(obj.ui.Figure);
obj.ui.PulsePalProgramEditorLabel.FontSize = 24;
obj.ui.PulsePalProgramEditorLabel.FontWeight = 'bold';
obj.ui.PulsePalProgramEditorLabel.Position = [13 430 297 32];
obj.ui.PulsePalProgramEditorLabel.Text = 'Pulse Pal Program Editor';

% Create FIREButton
obj.ui.FIREButton = uibutton(obj.ui.Figure, 'push');
obj.ui.FIREButton.Tooltip = {'Trigger the selected output channels'};
obj.ui.FIREButton.Position = [668 425 46 44];
obj.ui.FIREButton.Text = 'FIRE';

% Create CheckBox_TriggerCh1
obj.ui.CheckBox_TriggerCh1 = uicheckbox(obj.ui.Figure);
obj.ui.CheckBox_TriggerCh1.Text = '';
obj.ui.CheckBox_TriggerCh1.Position = [561 440 16 14];

% Create CheckBox_TriggerCh2
obj.ui.CheckBox_TriggerCh2 = uicheckbox(obj.ui.Figure);
obj.ui.CheckBox_TriggerCh2.Text = '';
obj.ui.CheckBox_TriggerCh2.Position = [587 440 16 14];

% Create CheckBox_TriggerCh3
obj.ui.CheckBox_TriggerCh3 = uicheckbox(obj.ui.Figure);
obj.ui.CheckBox_TriggerCh3.Text = '';
obj.ui.CheckBox_TriggerCh3.Position = [614 440 16 14];

% Create CheckBox_TriggerCh4
obj.ui.CheckBox_TriggerCh4 = uicheckbox(obj.ui.Figure);
obj.ui.CheckBox_TriggerCh4.Text = '';
obj.ui.CheckBox_TriggerCh4.Position = [640 440 16 14];

% Create HWLabel
obj.ui.HWLabel = uilabel(obj.ui.Figure);
obj.ui.HWLabel.Position = [14 2 128 22];
obj.ui.HWLabel.Text = ['HW: Pulse Pal v' num2str(obj.info.hardwareVersion)];

% Create TriggerChannelsLabel
obj.ui.TriggerChannelsLabel = uilabel(obj.ui.Figure);
obj.ui.TriggerChannelsLabel.FontWeight = 'bold';
obj.ui.TriggerChannelsLabel.Position = [448 437 107 22];
obj.ui.TriggerChannelsLabel.Text = 'Trigger Channels:';

% Create Label
obj.ui.Label = uilabel(obj.ui.Figure);
obj.ui.Label.FontWeight = 'bold';
obj.ui.Label.Position = [564 455 10 16];
obj.ui.Label.Text = '1';

% Create Label_2
obj.ui.Label_2 = uilabel(obj.ui.Figure);
obj.ui.Label_2.FontWeight = 'bold';
obj.ui.Label_2.Position = [590 455 10 17];
obj.ui.Label_2.Text = '2';

% Create Label_3
obj.ui.Label_3 = uilabel(obj.ui.Figure);
obj.ui.Label_3.FontWeight = 'bold';
obj.ui.Label_3.Position = [617 455 10 17];
obj.ui.Label_3.Text = '3';

% Create Label_4
obj.ui.Label_4 = uilabel(obj.ui.Figure);
obj.ui.Label_4.FontWeight = 'bold';
obj.ui.Label_4.Position = [643 455 10 16];
obj.ui.Label_4.Text = '4';

% Create PortLabel
obj.ui.PortLabel = uilabel(obj.ui.Figure);
obj.ui.PortLabel.Position = [241 2 160 22];
obj.ui.PortLabel.Text = ['Port: ' char(obj.Port.Port)];

% Create FirmwareLabel
obj.ui.FirmwareLabel = uilabel(obj.ui.Figure);
obj.ui.FirmwareLabel.Position = [133 2 78 22];
obj.ui.FirmwareLabel.Text = ['Firmware: v' num2str(obj.info.firmwareVersion)];

% Show the figure after all components are created
obj.ui.Figure.Visible = 'on';
end