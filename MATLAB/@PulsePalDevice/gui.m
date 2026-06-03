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
obj.ui.PushTool_RestoreParams.ClickedCallback = @(h,e)restoreDefaults(obj);

% Create PushTool_LoadProgram
obj.ui.PushTool_LoadProgram = uipushtool(obj.ui.Toolbar);
obj.ui.PushTool_LoadProgram.Tooltip = {'Open Program'};
obj.ui.PushTool_LoadProgram.Icon = fullfile(matlabroot,'toolbox','matlab','icons','file_open.png');
obj.ui.PushTool_LoadProgram.ClickedCallback = @(h,e)openProgram(obj);

% Create PushTool_SaveProgram
obj.ui.PushTool_SaveProgram = uipushtool(obj.ui.Toolbar);
obj.ui.PushTool_SaveProgram.Tooltip = {'Save Program'};
obj.ui.PushTool_SaveProgram.Icon = fullfile(matlabroot,'toolbox','matlab','icons','file_save.png');
obj.ui.PushTool_SaveProgram.ClickedCallback = @(h,e)saveProgram(obj);

% Create PushTool_UploadProgram
obj.ui.PushTool_UploadProgram = uipushtool(obj.ui.Toolbar);
obj.ui.PushTool_UploadProgram.Tooltip = {'Load program to device'};
obj.ui.PushTool_UploadProgram.Icon = fullfile(matlabroot,'toolbox','matlab','icons','boardicon.gif');
obj.ui.PushTool_UploadProgram.ClickedCallback = @(h,e)uploadProgram(obj);

% Create OutputChannelsPanel
obj.ui.OutputChannelsPanel = uipanel(obj.ui.Figure);
obj.ui.OutputChannelsPanel.Title = 'Output Channels';
obj.ui.OutputChannelsPanel.FontWeight = 'bold';
obj.ui.OutputChannelsPanel.Position = [13 251 700 164];

% Create PulseTypeLabel
obj.ui.PulseTypeLabel = uilabel(obj.ui.OutputChannelsPanel);
obj.ui.PulseTypeLabel.Position = [107 111 64 22];
obj.ui.PulseTypeLabel.Text = 'Pulse Type';

% Create DropDown_PulseType
obj.ui.DropDown_PulseType = uidropdown(obj.ui.OutputChannelsPanel);
obj.ui.DropDown_PulseType.Items = {'Monophasic', 'Biphasic'};
obj.ui.DropDown_PulseType.Tooltip = {'Biphasic pulses add an interval at the resting voltage and then a second phase to each pulse'};
obj.ui.DropDown_PulseType.Position = [87 84 100 22];
obj.ui.DropDown_PulseType.Value = 'Monophasic';
obj.ui.DropDown_PulseType.ValueChangedFcn = @(h,e)ui_SetPulseType(obj);

% Create CustomTrainIDLabel_2
obj.ui.CustomTrainIDLabel_2 = uilabel(obj.ui.OutputChannelsPanel);
obj.ui.CustomTrainIDLabel_2.Position = [455 111 92 22];
obj.ui.CustomTrainIDLabel_2.Text = 'Custom Train ID';

% Create DropDown_CustomTrainID
obj.ui.DropDown_CustomTrainID = uidropdown(obj.ui.OutputChannelsPanel);
obj.ui.DropDown_CustomTrainID.Items = {'0 (None)', '1', '2', '3', '4'};
obj.ui.DropDown_CustomTrainID.Position = [458 84 82 22];
obj.ui.DropDown_CustomTrainID.Value = '0 (None)';
obj.ui.DropDown_CustomTrainID.ValueChangedFcn = @(h,e)ui_SetCustomTrainID(obj);

% Create CustomTrainofLabel
obj.ui.CustomTrainofLabel = uilabel(obj.ui.OutputChannelsPanel);
obj.ui.CustomTrainofLabel.Position = [558 111 90 22];
obj.ui.CustomTrainofLabel.Text = 'Custom Train of';

% Create DropDown_CustomTrainTarget
obj.ui.DropDown_CustomTrainTarget = uidropdown(obj.ui.OutputChannelsPanel);
obj.ui.DropDown_CustomTrainTarget.Items = {'Pulses', 'Bursts'};
obj.ui.DropDown_CustomTrainTarget.Enable = 'off';
obj.ui.DropDown_CustomTrainTarget.Tooltip = {'Custom train timestamps can indicate the onset of either each pulse, or each burst of pulses'};
obj.ui.DropDown_CustomTrainTarget.Position = [560 84 82 22];
obj.ui.DropDown_CustomTrainTarget.Value = 'Pulses';
obj.ui.DropDown_CustomTrainTarget.ValueChangedFcn = @(h,e)ui_SetCustomTrainTarget(obj);

% Create LoopLabel
obj.ui.LoopLabel = uilabel(obj.ui.OutputChannelsPanel);
obj.ui.LoopLabel.Position = [659 111 32 22];
obj.ui.LoopLabel.Text = 'Loop';

% Create CheckBox_CustomTrainLoop
obj.ui.CheckBox_CustomTrainLoop = uicheckbox(obj.ui.OutputChannelsPanel);
obj.ui.CheckBox_CustomTrainLoop.Tooltip = {'If enabled, custom pulse train will loop until the pulse train duration (Train (s) below)'};
obj.ui.CheckBox_CustomTrainLoop.Enable = 'off';
obj.ui.CheckBox_CustomTrainLoop.Text = '';
obj.ui.CheckBox_CustomTrainLoop.Position = [665 84 17 22];
obj.ui.CheckBox_CustomTrainLoop.ValueChangedFcn = @(h,e)ui_SetCustomTrainLoop(obj);

% Create RestingVoltsLabel
obj.ui.Phase1VoltsLabel = uilabel(obj.ui.OutputChannelsPanel);
obj.ui.Phase1VoltsLabel.Position = [207 111 83 22];
obj.ui.Phase1VoltsLabel.Text = 'Resting (V)';

% Create Phase1VoltsLabel
obj.ui.Phase1VoltsLabel = uilabel(obj.ui.OutputChannelsPanel);
obj.ui.Phase1VoltsLabel.Position = [290 111 83 22];
obj.ui.Phase1VoltsLabel.Text = 'Phase1 (V)';

% Create Phase2VoltsLabel
obj.ui.Phase2VoltsLabel = uilabel(obj.ui.OutputChannelsPanel);
obj.ui.Phase2VoltsLabel.Position = [373 111 83 22];
obj.ui.Phase2VoltsLabel.Text = 'Phase2 (V)';

% Create EditField_RestingVoltage
obj.ui.EditField_RestingVoltage = uieditfield(obj.ui.OutputChannelsPanel, 'numeric');
obj.ui.EditField_RestingVoltage.HorizontalAlignment = 'center';
obj.ui.EditField_RestingVoltage.Enable = 'on';
obj.ui.EditField_RestingVoltage.Tooltip = {'Voltage while not delivering a pulse (V)'};
obj.ui.EditField_RestingVoltage.Position = [205 84 65 22];
obj.ui.EditField_RestingVoltage.ValueChangedFcn = @(h,e)ui_setNumericOutputParam(obj, 'RestingVoltage');

% Create EditField_Phase1Voltage
obj.ui.EditField_Phase1Voltage = uieditfield(obj.ui.OutputChannelsPanel, 'numeric');
obj.ui.EditField_Phase1Voltage.HorizontalAlignment = 'center';
obj.ui.EditField_Phase1Voltage.Tooltip = {'Voltage of the first phase of each pulse (V)'};
obj.ui.EditField_Phase1Voltage.Position = [288 84 65 22];
obj.ui.EditField_Phase1Voltage.ValueChangedFcn = @(h,e)ui_setNumericOutputParam(obj, 'Phase1Voltage');

% Create EditField_Phase2Voltage
obj.ui.EditField_Phase2Voltage = uieditfield(obj.ui.OutputChannelsPanel, 'numeric');
obj.ui.EditField_Phase2Voltage.HorizontalAlignment = 'center';
obj.ui.EditField_Phase2Voltage.Enable = 'off';
obj.ui.EditField_Phase2Voltage.Tooltip = {'Voltage of the second phase of each pulse (V)'};
obj.ui.EditField_Phase2Voltage.Position = [371 84 65 22];
obj.ui.EditField_Phase2Voltage.ValueChangedFcn = @(h,e)ui_setNumericOutputParam(obj, 'Phase2Voltage');

% Create Phase1sLabel
obj.ui.Phase1sLabel = uilabel(obj.ui.OutputChannelsPanel);
obj.ui.Phase1sLabel.Position = [19 49 63 22];
obj.ui.Phase1sLabel.Text = 'Phase1 (s)';

% Create EditField_Phase1Duration
obj.ui.EditField_Phase1Duration = uieditfield(obj.ui.OutputChannelsPanel, 'numeric');
obj.ui.EditField_Phase1Duration.HorizontalAlignment = 'center';
obj.ui.EditField_Phase1Duration.Tooltip = {'Duration of the first phase of each pulse (s)'};
obj.ui.EditField_Phase1Duration.Position = [14 20 72 22];
obj.ui.EditField_Phase1Duration.ValueChangedFcn = @(h,e)ui_setNumericOutputParam(obj, 'Phase1Duration');

% Create EditField_InterPhaseInterval
obj.ui.EditField_InterPhaseInterval = uieditfield(obj.ui.OutputChannelsPanel, 'numeric');
obj.ui.EditField_InterPhaseInterval.HorizontalAlignment = 'center';
obj.ui.EditField_InterPhaseInterval.Enable = 'off';
obj.ui.EditField_InterPhaseInterval.Tooltip = {'Interval between pulse phases (s)'};
obj.ui.EditField_InterPhaseInterval.Position = [101 20 72 22];
obj.ui.EditField_InterPhaseInterval.ValueChangedFcn = @(h,e)ui_setNumericOutputParam(obj, 'InterPhaseInterval');

% Create EditField_Phase2Duration
obj.ui.EditField_Phase2Duration = uieditfield(obj.ui.OutputChannelsPanel, 'numeric');
obj.ui.EditField_Phase2Duration.HorizontalAlignment = 'center';
obj.ui.EditField_Phase2Duration.Enable = 'off';
obj.ui.EditField_Phase2Duration.Tooltip = {'Duration of the second phase of each pulse (s)'};
obj.ui.EditField_Phase2Duration.Position = [188 20 72 22];
obj.ui.EditField_Phase2Duration.ValueChangedFcn = @(h,e)ui_setNumericOutputParam(obj, 'Phase2Duration');

% Create EditField_InterPulseInterval
obj.ui.EditField_InterPulseInterval = uieditfield(obj.ui.OutputChannelsPanel, 'numeric');
obj.ui.EditField_InterPulseInterval.HorizontalAlignment = 'center';
obj.ui.EditField_InterPulseInterval.Tooltip = {'Interval between pulse-end and the next pulse (s)'};
obj.ui.EditField_InterPulseInterval.Position = [276 20 72 22];
obj.ui.EditField_InterPulseInterval.ValueChangedFcn = @(h,e)ui_setNumericOutputParam(obj, 'InterPulseInterval');

% Create EditField_BurstDuration
obj.ui.EditField_BurstDuration = uieditfield(obj.ui.OutputChannelsPanel, 'numeric');
obj.ui.EditField_BurstDuration.HorizontalAlignment = 'center';
obj.ui.EditField_BurstDuration.Tooltip = {'Duration of pulse bursts (0 = no bursts, units = seconds)'};
obj.ui.EditField_BurstDuration.Position = [360 20 72 22];
obj.ui.EditField_BurstDuration.ValueChangedFcn = @(h,e)ui_setNumericOutputParam(obj, 'BurstDuration');

% Create EditField_InterBurstInterval
obj.ui.EditField_InterBurstInterval = uieditfield(obj.ui.OutputChannelsPanel, 'numeric');
obj.ui.EditField_InterBurstInterval.HorizontalAlignment = 'center';
obj.ui.EditField_InterBurstInterval.Tooltip = {'Interval betwen pulse bursts (s)'};
obj.ui.EditField_InterBurstInterval.Position = [445 20 72 22];
obj.ui.EditField_InterBurstInterval.ValueChangedFcn = @(h,e)ui_setNumericOutputParam(obj, 'InterBurstInterval');

% Create EditField_PulseTrainDuration
obj.ui.EditField_PulseTrainDuration = uieditfield(obj.ui.OutputChannelsPanel, 'numeric');
obj.ui.EditField_PulseTrainDuration.HorizontalAlignment = 'center';
obj.ui.EditField_PulseTrainDuration.Tooltip = {'Duration of the pulse train (s)'};
obj.ui.EditField_PulseTrainDuration.Position = [528 20 72 22];
obj.ui.EditField_PulseTrainDuration.ValueChangedFcn = @(h,e)ui_setNumericOutputParam(obj, 'PulseTrainDuration');

% Create EditField_PulseTrainDelay
obj.ui.EditField_PulseTrainDelay = uieditfield(obj.ui.OutputChannelsPanel, 'numeric');
obj.ui.EditField_PulseTrainDelay.HorizontalAlignment = 'center';
obj.ui.EditField_PulseTrainDelay.Tooltip = {'Delay from trigger to pulse train onset (s)'};
obj.ui.EditField_PulseTrainDelay.Position = [613 20 72 22];
obj.ui.EditField_PulseTrainDelay.ValueChangedFcn = @(h,e)ui_setNumericOutputParam(obj, 'PulseTrainDelay');

% Create ChannelButtonGroup_OutputChan
obj.ui.ChannelButtonGroup_OutputChan = uibuttongroup(obj.ui.OutputChannelsPanel);
obj.ui.ChannelButtonGroup_OutputChan.Tooltip = {'Select an output channel to edit'};
obj.ui.ChannelButtonGroup_OutputChan.BorderType = 'none';
obj.ui.ChannelButtonGroup_OutputChan.Title = 'Channel';
obj.ui.ChannelButtonGroup_OutputChan.Position = [12 80 60 62];
obj.ui.ChannelButtonGroup_OutputChan.SelectionChangedFcn = @(h,e)uiSelectOutputChannel(obj);

% Create RadioButton_OutputCh1
obj.ui.RadioButton_OutputCh1 = uiradiobutton(obj.ui.ChannelButtonGroup_OutputChan,'Interpreter','html');
obj.ui.RadioButton_OutputCh1.Text = '1';
obj.ui.RadioButton_OutputCh1.Position = [4 20 25 22];
obj.ui.RadioButton_OutputCh1.Value = true;

% Create RadioButton_OutputCh2
obj.ui.RadioButton_OutputCh2 = uiradiobutton(obj.ui.ChannelButtonGroup_OutputChan,'Interpreter','html');
obj.ui.RadioButton_OutputCh2.Text = '2';
obj.ui.RadioButton_OutputCh2.Position = [33 20 33 22];

% Create RadioButton_OutputCh3
obj.ui.RadioButton_OutputCh3 = uiradiobutton(obj.ui.ChannelButtonGroup_OutputChan,'Interpreter','html');
obj.ui.RadioButton_OutputCh3.Text = '3';
obj.ui.RadioButton_OutputCh3.Position = [4 0 25 22];

% Create RadioButton_OutputCh4
obj.ui.RadioButton_OutputCh4 = uiradiobutton(obj.ui.ChannelButtonGroup_OutputChan,'Interpreter','html');
obj.ui.RadioButton_OutputCh4.Text = '4';
obj.ui.RadioButton_OutputCh4.Position = [33 0 29 22];

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
obj.ui.ChannelButtonGroup_TriggerChan.SelectionChangedFcn = @(h,e)uiSelectTriggerChannel(obj);

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
obj.ui.DropDown_TriggerMode.ValueChangedFcn = @(h,e)uiSelectTriggerMode(obj);

% Create CheckBox_LinkToOutputCh1
obj.ui.CheckBox_LinkToOutputCh1 = uicheckbox(obj.ui.TriggerChannelsPanel);
obj.ui.CheckBox_LinkToOutputCh1.Tooltip = {'Link trigger channel to output channel 1'};
obj.ui.CheckBox_LinkToOutputCh1.Text = 'Ch1';
obj.ui.CheckBox_LinkToOutputCh1.Position = [223 9 44 22];
obj.ui.CheckBox_LinkToOutputCh1.ValueChangedFcn = @(h,e)uiSetTriggerLink(obj, 1);

% Create CheckBox_LinkToOutputCh2
obj.ui.CheckBox_LinkToOutputCh2 = uicheckbox(obj.ui.TriggerChannelsPanel);
obj.ui.CheckBox_LinkToOutputCh2.Tooltip = {'Link trigger channel to output channel 2'};
obj.ui.CheckBox_LinkToOutputCh2.Text = 'Ch2';
obj.ui.CheckBox_LinkToOutputCh2.Position = [283 9 44 22];
obj.ui.CheckBox_LinkToOutputCh2.ValueChangedFcn = @(h,e)uiSetTriggerLink(obj, 2);

% Create CheckBox_LinkToOutputCh3
obj.ui.CheckBox_LinkToOutputCh3 = uicheckbox(obj.ui.TriggerChannelsPanel);
obj.ui.CheckBox_LinkToOutputCh3.Tooltip = {'Link trigger channel to output channel 3'};
obj.ui.CheckBox_LinkToOutputCh3.Text = 'Ch3';
obj.ui.CheckBox_LinkToOutputCh3.Position = [340 9 44 22];
obj.ui.CheckBox_LinkToOutputCh3.ValueChangedFcn = @(h,e)uiSetTriggerLink(obj, 3);

% Create CheckBox_LinkToOutputCh4
obj.ui.CheckBox_LinkToOutputCh4 = uicheckbox(obj.ui.TriggerChannelsPanel);
obj.ui.CheckBox_LinkToOutputCh4.Tooltip = {'Link trigger channel to output channel 4'};
obj.ui.CheckBox_LinkToOutputCh4.Text = 'Ch4';
obj.ui.CheckBox_LinkToOutputCh4.Position = [397 9 44 22];
obj.ui.CheckBox_LinkToOutputCh4.ValueChangedFcn = @(h,e)uiSetTriggerLink(obj, 4);

% Create CustomPulseTrainsPanel
obj.ui.CustomPulseTrainsPanel = uipanel(obj.ui.Figure);
obj.ui.CustomPulseTrainsPanel.Title = 'Custom Pulse Trains';
obj.ui.CustomPulseTrainsPanel.FontWeight = 'bold';
obj.ui.CustomPulseTrainsPanel.Position = [13 24 700 115];

% Create ListBox_CustomTrainID - Custom Train Editor
obj.ui.ListBox_CustomTrainID = uilistbox(obj.ui.CustomPulseTrainsPanel);
obj.ui.ListBox_CustomTrainID.Items = {'1', '2', '3', '4'};
obj.ui.ListBox_CustomTrainID.Enable = 'off';
obj.ui.ListBox_CustomTrainID.Tooltip = {'Select the custom train to program'};
obj.ui.ListBox_CustomTrainID.Position = [15 22 100 37];
obj.ui.ListBox_CustomTrainID.Value = '1';
obj.ui.ListBox_CustomTrainID.ValueChangedFcn = @(h,e)ui_SetCustomTrainView(obj);

% Create CustomTrainIDLabel - Custom Train Editor
obj.ui.CustomTrainIDLabel = uilabel(obj.ui.CustomPulseTrainsPanel);
obj.ui.CustomTrainIDLabel.Position = [15 64 92 22];
obj.ui.CustomTrainIDLabel.Text = 'Custom Train ID';

% Create TextArea_CustomTrainTimestamps
obj.ui.TextArea_CustomTrainTimestamps = uitextarea(obj.ui.CustomPulseTrainsPanel);
obj.ui.TextArea_CustomTrainTimestamps.Enable = 'off';
obj.ui.TextArea_CustomTrainTimestamps.Tooltip = {'Enter the onset time of each pulse in the custom pulse train (comma delimited, units = seconds)'};
obj.ui.TextArea_CustomTrainTimestamps.Position = [133 13 271 47];
obj.ui.TextArea_CustomTrainTimestamps.ValueChangedFcn = @(h,e)uiSetCustomTimestamps(obj);

% Create TimestampssLabel
obj.ui.TimestampssLabel = uilabel(obj.ui.CustomPulseTrainsPanel);
obj.ui.TimestampssLabel.Position = [133 64 87 22];
obj.ui.TimestampssLabel.Text = 'Timestamps (s)';

% Create TextArea_CustomTrainVoltages
obj.ui.TextArea_CustomTrainVoltages = uitextarea(obj.ui.CustomPulseTrainsPanel);
obj.ui.TextArea_CustomTrainVoltages.Enable = 'off';
obj.ui.TextArea_CustomTrainVoltages.Tooltip = {'Enter the voltage of each pulse in the custom pulse train (comma delimited, units = volts)'};
obj.ui.TextArea_CustomTrainVoltages.Position = [416 13 271 47];
obj.ui.TextArea_CustomTrainVoltages.ValueChangedFcn = @(h,e)uiSetCustomVoltages(obj);

% Create VoltagesVLabel
obj.ui.VoltagesVLabel = uilabel(obj.ui.CustomPulseTrainsPanel);
obj.ui.VoltagesVLabel.Position = [417 64 70 22];
obj.ui.VoltagesVLabel.Text = 'Voltages (V)';

% Create PulsePalProgramEditorLabel
obj.ui.PulsePalProgramEditorLabel = uilabel(obj.ui.Figure);
obj.ui.PulsePalProgramEditorLabel.FontSize = 24;
obj.ui.PulsePalProgramEditorLabel.FontWeight = 'bold';
obj.ui.PulsePalProgramEditorLabel.Position = [13 432 297 32];
obj.ui.PulsePalProgramEditorLabel.Text = 'Pulse Pal Program Editor';

% Create FIREButton
obj.ui.FIREButton = uibutton(obj.ui.Figure, 'push');
obj.ui.FIREButton.Tooltip = {'Trigger the selected output channels'};
obj.ui.FIREButton.Position = [668 425 46 44];
obj.ui.FIREButton.Text = 'FIRE';
obj.ui.FIREButton.ButtonPushedFcn = @(h,e)uiTrigger(obj);

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

% Create StatusLabel
obj.ui.StatusLabel = uilabel(obj.ui.Figure);
obj.ui.StatusLabel.Position = [413 2 300 22];
obj.ui.StatusLabel.HorizontalAlignment = 'right';
obj.ui.StatusLabel.Text = 'Status: GUI Loaded';

% Create local copy of gui parameters
obj.ui.params = obj.defaultParams;

% Initialize custom pulse trains
obj.ui.customTrain = struct;
obj.ui.customTrain.timestamps = repmat({''}, 1, 4);
obj.ui.customTrain.voltages = repmat({''}, 1, 4);

% Initialize other GUI variables
obj.ui.lastProgramPath = '';

% Push the local copy to the GUI
setUIParams(obj);

% Show the figure after all components are created
obj.ui.Figure.Visible = 'on';
end

function setUIParams(obj)
params = obj.ui.params;
% Determine selected channels
outChanSelected = str2double(obj.ui.ChannelButtonGroup_OutputChan.SelectedObject.Text);
trigChanSelected = str2double(obj.ui.ChannelButtonGroup_TriggerChan.SelectedObject.Text);

% Set UI fields
obj.ui.DropDown_PulseType.ValueIndex = params.isBiphasic(outChanSelected)+1;
obj.ui.EditField_RestingVoltage.Value = params.restingVoltage(outChanSelected);
obj.ui.EditField_Phase1Voltage.Value = params.phase1Voltage(outChanSelected);
obj.ui.EditField_Phase2Voltage.Value = params.phase2Voltage(outChanSelected);
obj.ui.DropDown_CustomTrainID.ValueIndex = params.customTrainID(outChanSelected)+1;
obj.ui.DropDown_CustomTrainTarget.ValueIndex = params.customTrainTarget(outChanSelected)+1;
obj.ui.CheckBox_CustomTrainLoop.Value = params.customTrainLoop(outChanSelected);
obj.ui.EditField_Phase1Duration.Value = params.phase1Duration(outChanSelected);
obj.ui.EditField_InterPhaseInterval.Value = params.interPhaseInterval(outChanSelected);
obj.ui.EditField_Phase2Duration.Value = params.phase2Duration(outChanSelected);
obj.ui.EditField_InterPulseInterval.Value = params.interPulseInterval(outChanSelected);
obj.ui.EditField_BurstDuration.Value = params.burstDuration(outChanSelected);
obj.ui.EditField_InterBurstInterval.Value = params.interBurstInterval(outChanSelected);
obj.ui.EditField_PulseTrainDuration.Value = params.pulseTrainDuration(outChanSelected);
obj.ui.EditField_PulseTrainDelay.Value = params.pulseTrainDelay(outChanSelected);
obj.ui.DropDown_TriggerMode.ValueIndex = params.triggerMode(trigChanSelected)+1;
switch trigChanSelected
    case 1
        obj.ui.CheckBox_LinkToOutputCh1.Value = params.linkTriggerChannel1(1);
        obj.ui.CheckBox_LinkToOutputCh2.Value = params.linkTriggerChannel1(2);
        obj.ui.CheckBox_LinkToOutputCh3.Value = params.linkTriggerChannel1(3);
        obj.ui.CheckBox_LinkToOutputCh4.Value = params.linkTriggerChannel1(4);
    case 2
        obj.ui.CheckBox_LinkToOutputCh1.Value = params.linkTriggerChannel2(1);
        obj.ui.CheckBox_LinkToOutputCh2.Value = params.linkTriggerChannel2(2);
        obj.ui.CheckBox_LinkToOutputCh3.Value = params.linkTriggerChannel2(3);
        obj.ui.CheckBox_LinkToOutputCh4.Value = params.linkTriggerChannel2(4);
end
enableFields(obj);
end

function enableFields(obj)
params = obj.ui.params;
outChanSelected = str2double(obj.ui.ChannelButtonGroup_OutputChan.SelectedObject.Text);
obj.ui.EditField_Phase2Voltage.Enable = params.isBiphasic(outChanSelected);
obj.ui.EditField_InterPhaseInterval.Enable = params.isBiphasic(outChanSelected);
obj.ui.EditField_Phase2Duration.Enable = params.isBiphasic(outChanSelected);
usesCustomTrains = params.customTrainID(outChanSelected) > 0;
obj.ui.DropDown_CustomTrainTarget.Enable = usesCustomTrains;
obj.ui.CheckBox_CustomTrainLoop.Enable = usesCustomTrains;
obj.ui.ListBox_CustomTrainID.Enable = usesCustomTrains;
obj.ui.TextArea_CustomTrainTimestamps.Enable = usesCustomTrains;
obj.ui.TextArea_CustomTrainVoltages.Enable = usesCustomTrains;
end

function uiTrigger(obj)
triggerLogic = zeros(1,4);
triggerLogic(1) = obj.ui.CheckBox_TriggerCh1.Value;
triggerLogic(2) = obj.ui.CheckBox_TriggerCh2.Value;
triggerLogic(3) = obj.ui.CheckBox_TriggerCh3.Value;
triggerLogic(4) = obj.ui.CheckBox_TriggerCh4.Value;
chan2Trigger = find(triggerLogic);
if ~isempty(chan2Trigger)
    obj.trigger(chan2Trigger);
    obj.ui.StatusLabel.Text = 'Status: Output Channels Triggered';
end
end

function uiSelectOutputChannel(obj)
setUIParams(obj);
end

function uiSelectTriggerChannel(obj)
setUIParams(obj);
end

% ---- Parameter edit callback functions ----

function ui_setNumericOutputParam(obj, ParamName)
chan = str2double(obj.ui.ChannelButtonGroup_OutputChan.SelectedObject.Text);
switch ParamName
    case 'RestingVoltage'
        obj.ui.params.restingVoltage(chan) = obj.ui.EditField_RestingVoltage.Value;
    case 'Phase1Voltage'
        obj.ui.params.phase1Voltage(chan) = obj.ui.EditField_Phase1Voltage.Value;
    case 'Phase2Voltage'
        obj.ui.params.phase2Voltage(chan) = obj.ui.EditField_Phase2Voltage.Value;
    case 'Phase1Duration'
        obj.ui.params.phase1Duration(chan) = obj.ui.EditField_Phase1Duration.Value;
    case 'InterPhaseInterval'
        obj.ui.params.interPhaseInterval(chan) = obj.ui.EditField_InterPhaseInterval.Value;
    case 'Phase2Duration'
        obj.ui.params.phase2Duration(chan) = obj.ui.EditField_Phase2Duration.Value;
    case 'InterPulseInterval'
        obj.ui.params.interPulseInterval(chan) = obj.ui.EditField_InterPulseInterval.Value;
    case 'BurstDuration'
        obj.ui.params.burstDuration(chan) = obj.ui.EditField_BurstDuration.Value;
    case 'InterBurstInterval'
        obj.ui.params.interBurstInterval(chan) = obj.ui.EditField_InterBurstInterval.Value;
    case 'PulseTrainDuration'
        obj.ui.params.pulseTrainDuration(chan) = obj.ui.EditField_PulseTrainDuration.Value;
    case 'PulseTrainDelay'
        obj.ui.params.pulseTrainDelay(chan) = obj.ui.EditField_PulseTrainDelay.Value;
end
end

function ui_SetPulseType(obj)
chan = str2double(obj.ui.ChannelButtonGroup_OutputChan.SelectedObject.Text);
newPulseType = obj.ui.DropDown_PulseType.ValueIndex;
obj.ui.params.isBiphasic(chan) = double(newPulseType == 2);
enableFields(obj);
end

function ui_SetCustomTrainID(obj)
chan = str2double(obj.ui.ChannelButtonGroup_OutputChan.SelectedObject.Text);
newID = obj.ui.DropDown_CustomTrainID.ValueIndex;
obj.ui.params.customTrainID(chan) = newID-1;
enableFields(obj);
end

function ui_SetCustomTrainTarget(obj)
chan = str2double(obj.ui.ChannelButtonGroup_OutputChan.SelectedObject.Text);
newTarget = obj.ui.DropDown_CustomTrainTarget.ValueIndex;
obj.ui.params.customTrainTarget(chan) = newTarget;
end

function ui_SetCustomTrainLoop(obj)
chan = str2double(obj.ui.ChannelButtonGroup_OutputChan.SelectedObject.Text);
loopEnabled = obj.ui.CheckBox_CustomTrainLoop.Value;
obj.ui.params.customTrainLoop(chan) = double(loopEnabled);
end

function uiSelectTriggerMode(obj)
chan = str2double(obj.ui.ChannelButtonGroup_TriggerChan.SelectedObject.Text);
newValue = obj.ui.DropDown_TriggerMode.ValueIndex;
obj.ui.params.triggerMode(chan) = newValue-1;
end

function uiSetTriggerLink(obj, index)
chan = str2double(obj.ui.ChannelButtonGroup_TriggerChan.SelectedObject.Text);
switch index
    case 1
        value = obj.ui.CheckBox_LinkToOutputCh1.Value;
    case 2
        value = obj.ui.CheckBox_LinkToOutputCh2.Value;
    case 3
        value = obj.ui.CheckBox_LinkToOutputCh3.Value;
    case 4
        value = obj.ui.CheckBox_LinkToOutputCh4.Value;
end
switch chan
    case 1
        obj.ui.params.linkTriggerChannel1(index) = value;
    case 2
        obj.ui.params.linkTriggerChannel2(index) = value;
end
end

function uiSetCustomTimestamps(obj)
trainID = str2double(obj.ui.ListBox_CustomTrainID.Value);
timestamps = obj.ui.TextArea_CustomTrainTimestamps.Value;

% Validate format
timestampString = char(timestamps{1});
pattern = '^\s*(\d+(\.\d*)?|\.\d+)(\s*,\s*(\d+(\.\d*)?|\.\d+))*\s*$';
if isempty(regexp(timestampString, pattern, 'once'))
    errordlg('Timestamps must be a comma-delimited list of pulse onset times, given in seconds.')
end

% Assign
obj.ui.customTrain.timestamps{trainID} = timestamps{1};
end

function uiSetCustomVoltages(obj)
trainID = str2double(obj.ui.ListBox_CustomTrainID.Value);
voltages = obj.ui.TextArea_CustomTrainVoltages.Value;

% Validate format
voltageString = char(voltages{1});
pattern = '^\s*[+-]?(\d+(\.\d*)?|\.\d+)(\s*,\s*[+-]?(\d+(\.\d*)?|\.\d+))*\s*$';
if isempty(regexp(voltageString, pattern, 'once'))
    errordlg('Voltages must be a comma-delimited list of pulse voltages, given in volts.')
end

% Assign
obj.ui.customTrain.voltages{trainID} = voltages{1};
end

function ui_SetCustomTrainView(obj)
trainID = str2double(obj.ui.ListBox_CustomTrainID.Value);
obj.ui.TextArea_CustomTrainTimestamps.Value = obj.ui.customTrain.timestamps{trainID};
obj.ui.TextArea_CustomTrainVoltages.Value = obj.ui.customTrain.voltages{trainID};
end

function restoreDefaults(obj)
resetGUISelections(obj);
obj.ui.params = obj.defaultParams;
setUIParams(obj);
obj.ui.customTrain = struct;
obj.ui.customTrain.timestamps = repmat({''}, 1, 4);
obj.ui.customTrain.voltages = repmat({''}, 1, 4);
obj.ui.TextArea_CustomTrainTimestamps.Value = '';
obj.ui.TextArea_CustomTrainVoltages.Value = '';
obj.ui.StatusLabel.Text = 'Status: Default Program Restored';
end

function uploadProgram(obj)
% Sync paramaters from GUI to user fields
autoSyncState = obj.autoSync;
obj.autoSync = 'off';
obj.importParams(obj.ui.params);
obj.syncAllParams;
obj.autoSync = autoSyncState;

% Sync custom waveforms (if applicable)
for iTrain = 1:obj.info.nCustomPulseTrains
    timestampString = obj.ui.customTrain.timestamps{iTrain};
    voltageString = obj.ui.customTrain.voltages{iTrain};
    if ~isempty(timestampString) || ~isempty(voltageString)
        timestamps = str2double(split(timestampString, ','))';
        voltages = str2double(split(voltageString, ','))';
        nTimestamps = length(timestamps);
        nVoltages = length(voltages);
        if nTimestamps ~= nVoltages
            errordlg(['Failed to load custom pulse train ' num2str(iTrain) ': the number of timestamps and voltages must match.'])
            error(['Failed to load custom pulse train ' num2str(iTrain) ': the number of timestamps and voltages must match.'])
        end
        if nTimestamps > 0
            obj.sendCustomTrain(iTrain, timestamps, voltages);
        end
    end
end
    obj.ui.StatusLabel.Text = 'Status: Program Loaded to Device';
end

function saveProgram(obj)
program = struct;
program.params = obj.ui.params;
program.customTrainTimestamps = cell(1,4);
program.customTrainVoltages = cell(1,4);
for iTrain = 1:4
    program.customTrainTimestamps{iTrain} = str2double(split(obj.ui.customTrain.timestamps{iTrain}, ','))';
    program.customTrainVoltages{iTrain} = str2double(split(obj.ui.customTrain.voltages{iTrain}, ','))';
end
program.deviceInfo = obj.info;
if isempty(obj.ui.lastProgramPath)
    [file,path] = uiputfile('PulsePalProgram.mat','Save program');
else
    [file,path] = uiputfile('PulsePalProgram.mat','Save program', obj.ui.lastProgramPath);
end
if ischar(file) && ischar(path)
    obj.ui.lastProgramPath = path;
    savepath = fullfile(path, file);
    save(savepath, 'program');
    obj.ui.StatusLabel.Text = 'Status: Program Saved';
end
end

function openProgram(obj)
if isempty(obj.ui.lastProgramPath)
    [file,path] = uigetfile('*.mat','Open program');
else
    [file,path] = uigetfile('*.mat','Open program', obj.ui.lastProgramPath);
end
if ischar(file) && ischar(path)
    obj.ui.lastProgramPath = path;
    newProgram = load(fullfile(path, file));
    isValidProgram = false;
    if isfield(newProgram, 'program') % Saved from PulsePalDevice object
        resetGUISelections(obj);
        program = newProgram.program;
        customTimestamps = cell(1,4);
        customVoltages = cell(1,4);
        for iTrain = 1:4 % Convert custom train timestamps and voltages to string
            thisTimestamp = program.customTrainTimestamps{iTrain};
            if isnan(thisTimestamp)
                customTimestamps{iTrain} = '';
            else
                customTimestamps{iTrain} = char(strjoin(string(thisTimestamp), ', '));
            end
            thisVoltage = program.customTrainVoltages{iTrain};
            if isnan(thisVoltage)
                customVoltages{iTrain} = '';
            else
                customVoltages{iTrain} = char(strjoin(string(thisVoltage), ', '));
            end
        end
        obj.ui.customTrain.timestamps = customTimestamps;
        obj.ui.customTrain.voltages = customVoltages;
        ui_SetCustomTrainView(obj);
        isValidProgram = true;
    elseif isfield(newProgram, 'ParameterMatrix') % Saved from legacy PulsePalGUI
        resetGUISelections(obj);
        program = struct;
        program.params = struct;
        matrix = newProgram.ParameterMatrix;
        program.params.isBiphasic = cell2mat(matrix(2,2:5));
        program.params.phase1Voltage = cell2mat(matrix(3,2:5));
        program.params.phase2Voltage = cell2mat(matrix(4,2:5));
        program.params.restingVoltage = cell2mat(matrix(18,2:5));
        program.params.phase1Duration = cell2mat(matrix(5,2:5));
        program.params.interPhaseInterval = cell2mat(matrix(6,2:5));
        program.params.phase2Duration = cell2mat(matrix(7,2:5));
        program.params.interPulseInterval = cell2mat(matrix(8,2:5));
        program.params.burstDuration = cell2mat(matrix(9,2:5));
        program.params.interBurstInterval = cell2mat(matrix(10,2:5));
        program.params.pulseTrainDuration = cell2mat(matrix(11,2:5));
        program.params.pulseTrainDelay = cell2mat(matrix(12,2:5));
        program.params.linkTriggerChannel1 = cell2mat(matrix(13,2:5));
        program.params.linkTriggerChannel2 = cell2mat(matrix(14,2:5));
        program.params.customTrainID = cell2mat(matrix(15,2:5));
        program.params.customTrainTarget = cell2mat(matrix(16,2:5));
        program.params.customTrainLoop = cell2mat(matrix(17,2:5));
        program.params.triggerMode = cell2mat(matrix(2,8:9));
        program.params.playbackMode = zeros(1,4);
        isValidProgram = true;
    end
    if ~isValidProgram
        errordlg(['Failed to open file: ' file ': unknown data format.'])
        error(['Failed to open file: ' file ': unknown data format.'])
    end
    obj.ui.params = program.params;
    setUIParams(obj);
    obj.ui.StatusLabel.Text = 'Status: Program Opened';
end
end

function resetGUISelections(obj)
obj.ui.ChannelButtonGroup_OutputChan.SelectedObject = obj.ui.RadioButton_OutputCh1;
obj.ui.ChannelButtonGroup_TriggerChan.SelectedObject = obj.ui.RadioButton_TriggerCh1;
obj.ui.ListBox_CustomTrainID.Value = '1';
end