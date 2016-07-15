%{
----------------------------------------------------------------------------

This file is part of the Sanworks Pulse Pal repository
Copyright (C) 2016 Sanworks LLC, Sound Beach, New York, USA

----------------------------------------------------------------------------

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, version 3.

This program is distributed  WITHOUT ANY WARRANTY and without even the 
implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  
See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
%}

function varargout = PulsePalGUI(varargin)
% PULSEPALGUI M-file for PulsePalGUI.fig
%      PULSEPALGUI, by itself, creates a new PULSEPALGUI or raises the existing
%      singleton*.
%
%      H = PULSEPALGUI returns the handle to a new PULSEPALGUI or the handle to
%      the existing singleton*.
%
%      PULSEPALGUI('CALLBACK',hObject,eventData,handles,...) calls the local
%      function named CALLBACK in PULSEPALGUI.M with the given input arguments.
%
%      PULSEPALGUI('Property','Value',...) creates a new PULSEPALGUI or raises the
%      existing singleton*.  Starting from the left, property value pairs are
%      applied to the GUI before PulsePalGUI_OpeningFcn gets called.  An
%      unrecognized property name or invalid value makes property application
%      stop.  All inputs are passed to PulsePalGUI_OpeningFcn via varargin.
%
%      *See GUI Options on GUIDE's Tools menu.  Choose "GUI allows only one
%      instance to run (singleton)".
%
% See also: GUIDE, GUIDATA, GUIHANDLES

% Edit the above text to modify the response to help PulsePalGUI

% Last Modified by GUIDE v2.5 15-Jul-2016 12:44:08

% Begin initialization code - DO NOT EDIT

% Tough noogies, MATLAB, I'm editing it.
global PulsePalSystem
if (PulsePalSystem.UsingOctave)
    error('The Pulse Pal GUI is currently available only in MATLAB.');
end
gui_Singleton = 1;
gui_State = struct('gui_Name',       mfilename, ...
                   'gui_Singleton',  gui_Singleton, ...
                   'gui_OpeningFcn', @PulsePalGUI_OpeningFcn, ...
                   'gui_OutputFcn',  @PulsePalGUI_OutputFcn, ...
                   'gui_LayoutFcn',  [] , ...
                   'gui_Callback',   []);
if nargin && ischar(varargin{1})
    gui_State.gui_Callback = str2func(varargin{1});
end

if nargout
    [varargout{1:nargout}] = gui_mainfcn(gui_State, varargin{:});
else
    gui_mainfcn(gui_State, varargin{:});
end
% End initialization code - DO NOT EDIT


% --- Executes just before PulsePalGUI is made visible.
function PulsePalGUI_OpeningFcn(hObject, eventdata, handles, varargin)
% This function has no output args, see OutputFcn.
% hObject    handle to figure
% eventdata  reserved - to be defined in a future version of MATLAB
% handles    structure with handles and user data (see GUIDATA)
% varargin   command line arguments to PulsePalGUI (see VARARGIN)

global PulsePalSystem
if ~isempty(PulsePalSystem)
    TemplatePath = fullfile(PulsePalSystem.PulsePalPath,'GUI','PulsePalProgram_Template.mat');
else
    error('Pulse Pal must be initialized before running the user interface. Type PulsePal at the command prompt.')
end

ha = axes('units','normalized', 'position',[0 0 1 1]);
uistack(ha,'bottom');
BG = imread('ProgrammerBG.bmp');
imagesc(BG); axis off;
load(TemplatePath);
handles.Matrix = ParameterMatrix;
handles.StimButtonGFX_Unpressed = imread('Stim_Unselected.bmp');
handles.StimButtonGFX_Pressed = imread('Stim_Selected.bmp');
set(handles.pushbutton7, 'CData', handles.StimButtonGFX_Unpressed);
if (~ispc && ~ismac)
    set(handles.listbox1, 'Position', [18 399 78 61]);
end


% Choose default command line output for PulsePalGUI
handles.output = hObject;

% Update handles structure
guidata(hObject, handles);
% MatrixRow2GUI(hObject, handles)
% UIWAIT makes PulsePalGUI wait for user response (see UIRESUME)
% uiwait(handles.figure1);


% --- Outputs from this function are returned to the command line.
function varargout = PulsePalGUI_OutputFcn(hObject, eventdata, handles) 
% varargout  cell array for returning output args (see VARARGOUT);
% hObject    handle to figure
% eventdata  reserved - to be defined in a future version of MATLAB
% handles    structure with handles and user data (see GUIDATA)

% Get default command line output from handles structure
varargout{1} = handles.output;


% --- Executes on selection change in popupmenu1.
function popupmenu1_Callback(hObject, eventdata, handles)
% hObject    handle to popupmenu1 (see GCBO)
% eventdata  reserved - to be defined in a future version of MATLAB
% handles    structure with handles and user data (see GUIDATA)

% Hints: contents = cellstr(get(hObject,'String')) returns popupmenu1 contents as cell array
%        contents{get(hObject,'Value')} returns selected item from popupmenu1


handles.LastElementChanged = handles.popupmenu1; guidata(hObject, handles);
GUI2MatrixRow(hObject, handles);

% --- Executes during object creation, after setting all properties.
function popupmenu1_CreateFcn(hObject, eventdata, handles)
% hObject    handle to popupmenu1 (see GCBO)
% eventdata  reserved - to be defined in a future version of MATLAB
% handles    empty - handles not created until after all CreateFcns called

% Hint: popupmenu controls usually have a white background on Windows.
%       See ISPC and COMPUTER.
if ispc && isequal(get(hObject,'BackgroundColor'), get(0,'defaultUicontrolBackgroundColor'))
    set(hObject,'BackgroundColor','white');
end




% --- Executes on button press in checkbox1.
function checkbox1_Callback(hObject, eventdata, handles)
% hObject    handle to checkbox1 (see GCBO)
% eventdata  reserved - to be defined in a future version of MATLAB
% handles    structure with handles and user data (see GUIDATA)

% Hint: get(hObject,'Value') returns toggle state of checkbox1
handles.LastElementChanged = handles.checkbox1; guidata(hObject, handles);
GUI2MatrixRow(hObject, handles);

% --- Executes on button press in checkbox2.
function checkbox2_Callback(hObject, eventdata, handles)
% hObject    handle to checkbox2 (see GCBO)
% eventdata  reserved - to be defined in a future version of MATLAB
% handles    structure with handles and user data (see GUIDATA)

% Hint: get(hObject,'Value') returns toggle state of checkbox2
handles.LastElementChanged = handles.checkbox2; guidata(hObject, handles);
GUI2MatrixRow(hObject, handles);

% --- Executes on selection change in popupmenu2.
function popupmenu2_Callback(hObject, eventdata, handles)
% hObject    handle to popupmenu2 (see GCBO)
% eventdata  reserved - to be defined in a future version of MATLAB
% handles    structure with handles and user data (see GUIDATA)

% Hints: contents = cellstr(get(hObject,'String')) returns popupmenu2 contents as cell array
%        contents{get(hObject,'Value')} returns selected item from popupmenu2

handles.LastElementChanged = handles.popupmenu2; guidata(hObject, handles);
GUI2MatrixRow(hObject, handles);

% --- Executes during object creation, after setting all properties.
function popupmenu2_CreateFcn(hObject, eventdata, handles)
% hObject    handle to popupmenu2 (see GCBO)
% eventdata  reserved - to be defined in a future version of MATLAB
% handles    empty - handles not created until after all CreateFcns called

% Hint: popupmenu controls usually have a white background on Windows.
%       See ISPC and COMPUTER.
if ispc && isequal(get(hObject,'BackgroundColor'), get(0,'defaultUicontrolBackgroundColor'))
    set(hObject,'BackgroundColor','white');
end


% --- Executes on selection change in popupmenu3.
function popupmenu3_Callback(hObject, eventdata, handles)
% hObject    handle to popupmenu3 (see GCBO)
% eventdata  reserved - to be defined in a future version of MATLAB
% handles    structure with handles and user data (see GUIDATA)

% Hints: contents = cellstr(get(hObject,'String')) returns popupmenu3 contents as cell array
%        contents{get(hObject,'Value')} returns selected item from popupmenu3
handles.LastElementChanged = handles.popupmenu3; guidata(hObject, handles);
GUI2MatrixRow(hObject, handles);

% --- Executes during object creation, after setting all properties.
function popupmenu3_CreateFcn(hObject, eventdata, handles)
% hObject    handle to popupmenu3 (see GCBO)
% eventdata  reserved - to be defined in a future version of MATLAB
% handles    empty - handles not created until after all CreateFcns called

% Hint: popupmenu controls usually have a white background on Windows.
%       See ISPC and COMPUTER.
if ispc && isequal(get(hObject,'BackgroundColor'), get(0,'defaultUicontrolBackgroundColor'))
    set(hObject,'BackgroundColor','white');
end



function edit11_Callback(hObject, eventdata, handles)
% hObject    handle to edit11 (see GCBO)
% eventdata  reserved - to be defined in a future version of MATLAB
% handles    structure with handles and user data (see GUIDATA)

% Hints: get(hObject,'String') returns contents of edit11 as text
%        str2double(get(hObject,'String')) returns contents of edit11 as a double


% --- Executes during object creation, after setting all properties.
function edit11_CreateFcn(hObject, eventdata, handles)
% hObject    handle to edit11 (see GCBO)
% eventdata  reserved - to be defined in a future version of MATLAB
% handles    empty - handles not created until after all CreateFcns called

% Hint: edit controls usually have a white background on Windows.
%       See ISPC and COMPUTER.
if ispc && isequal(get(hObject,'BackgroundColor'), get(0,'defaultUicontrolBackgroundColor'))
    set(hObject,'BackgroundColor','white');
end


% --- Executes on selection change in listbox1.
function listbox1_Callback(hObject, eventdata, handles)
% hObject    handle to listbox1 (see GCBO)
% eventdata  reserved - to be defined in a future version of MATLAB
% handles    structure with handles and user data (see GUIDATA)

% Hints: contents = cellstr(get(hObject,'String')) returns listbox1 contents as cell array
%        contents{get(hObject,'Value')} returns selected item from listbox1
MatrixRow2GUI(hObject, handles);

% --- Executes during object creation, after setting all properties.
function listbox1_CreateFcn(hObject, eventdata, handles)
% hObject    handle to listbox1 (see GCBO)
% eventdata  reserved - to be defined in a future version of MATLAB
% handles    empty - handles not created until after all CreateFcns called

% Hint: listbox controls usually have a white background on Windows.
%       See ISPC and COMPUTER.
if ispc && isequal(get(hObject,'BackgroundColor'), get(0,'defaultUicontrolBackgroundColor'))
    set(hObject,'BackgroundColor','white');
end






% --- Executes on selection change in listbox2.
function listbox2_Callback(hObject, eventdata, handles)
% hObject    handle to listbox2 (see GCBO)
% eventdata  reserved - to be defined in a future version of MATLAB
% handles    structure with handles and user data (see GUIDATA)

% Hints: contents = cellstr(get(hObject,'String')) returns listbox2 contents as cell array
%        contents{get(hObject,'Value')} returns selected item from listbox2
MatrixRow2GUI(hObject, handles);

% --- Executes during object creation, after setting all properties.
function listbox2_CreateFcn(hObject, eventdata, handles)
% hObject    handle to listbox2 (see GCBO)
% eventdata  reserved - to be defined in a future version of MATLAB
% handles    empty - handles not created until after all CreateFcns called

% Hint: listbox controls usually have a white background on Windows.
%       See ISPC and COMPUTER.
if ispc && isequal(get(hObject,'BackgroundColor'), get(0,'defaultUicontrolBackgroundColor'))
    set(hObject,'BackgroundColor','white');
end


% --- Executes on button press in pushbutton1.
function pushbutton1_Callback(hObject, eventdata, handles)
% hObject    handle to pushbutton1 (see GCBO)
% eventdata  reserved - to be defined in a future version of MATLAB
% handles    structure with handles and user data (see GUIDATA)


% --- Executes on selection change in popupmenu4.
function popupmenu4_Callback(hObject, eventdata, handles)
% hObject    handle to popupmenu4 (see GCBO)
% eventdata  reserved - to be defined in a future version of MATLAB
% handles    structure with handles and user data (see GUIDATA)

% Hints: contents = cellstr(get(hObject,'String')) returns popupmenu4 contents as cell array
%        contents{get(hObject,'Value')} returns selected item from popupmenu4


handles.LastElementChanged = handles.popupmenu4; guidata(hObject, handles);
GUI2MatrixRow(hObject, handles);
% --- Executes during object creation, after setting all properties.
function popupmenu4_CreateFcn(hObject, eventdata, handles)
% hObject    handle to popupmenu4 (see GCBO)
% eventdata  reserved - to be defined in a future version of MATLAB
% handles    empty - handles not created until after all CreateFcns called

% Hint: popupmenu controls usually have a white background on Windows.
%       See ISPC and COMPUTER.
if ispc && isequal(get(hObject,'BackgroundColor'), get(0,'defaultUicontrolBackgroundColor'))
    set(hObject,'BackgroundColor','white');
end






% --- Executes on button press in checkbox4.
function checkbox4_Callback(hObject, eventdata, handles)
% hObject    handle to checkbox4 (see GCBO)
% eventdata  reserved - to be defined in a future version of MATLAB
% handles    structure with handles and user data (see GUIDATA)

% Hint: get(hObject,'Value') returns toggle state of checkbox4
handles.LastElementChanged = handles.checkbox4; guidata(hObject, handles);
GUI2MatrixRow(hObject, handles);
% --- Executes on button press in checkbox5.
function checkbox5_Callback(hObject, eventdata, handles)
% hObject    handle to checkbox5 (see GCBO)
% eventdata  reserved - to be defined in a future version of MATLAB
% handles    structure with handles and user data (see GUIDATA)

% Hint: get(hObject,'Value') returns toggle state of checkbox5
handles.LastElementChanged = handles.checkbox5; guidata(hObject, handles);
GUI2MatrixRow(hObject, handles);



% --- Executes on button press in pushbutton2.
function pushbutton2_Callback(hObject, eventdata, handles)
% hObject    handle to pushbutton2 (see GCBO)
% eventdata  reserved - to be defined in a future version of MATLAB
% handles    structure with handles and user data (see GUIDATA)



function edit21_Callback(hObject, eventdata, handles)
% hObject    handle to edit21 (see GCBO)
% eventdata  reserved - to be defined in a future version of MATLAB
% handles    structure with handles and user data (see GUIDATA)

% Hints: get(hObject,'String') returns contents of edit21 as text
%        str2double(get(hObject,'String')) returns contents of edit21 as a double
handles.LastElementChanged = handles.edit21; guidata(hObject, handles);
GUI2MatrixRow(hObject, handles);

% --- Executes during object creation, after setting all properties.
function edit21_CreateFcn(hObject, eventdata, handles)
% hObject    handle to edit21 (see GCBO)
% eventdata  reserved - to be defined in a future version of MATLAB
% handles    empty - handles not created until after all CreateFcns called

% Hint: edit controls usually have a white background on Windows.
%       See ISPC and COMPUTER.
if ispc && isequal(get(hObject,'BackgroundColor'), get(0,'defaultUicontrolBackgroundColor'))
    set(hObject,'BackgroundColor','white');
end



function edit22_Callback(hObject, eventdata, handles)
% hObject    handle to edit22 (see GCBO)
% eventdata  reserved - to be defined in a future version of MATLAB
% handles    structure with handles and user data (see GUIDATA)

% Hints: get(hObject,'String') returns contents of edit22 as text
%        str2double(get(hObject,'String')) returns contents of edit22 as a double
handles.LastElementChanged = handles.edit22; guidata(hObject, handles);
GUI2MatrixRow(hObject, handles);

% --- Executes during object creation, after setting all properties.
function edit22_CreateFcn(hObject, eventdata, handles)
% hObject    handle to edit22 (see GCBO)
% eventdata  reserved - to be defined in a future version of MATLAB
% handles    empty - handles not created until after all CreateFcns called

% Hint: edit controls usually have a white background on Windows.
%       See ISPC and COMPUTER.
if ispc && isequal(get(hObject,'BackgroundColor'), get(0,'defaultUicontrolBackgroundColor'))
    set(hObject,'BackgroundColor','white');
end



function edit23_Callback(hObject, eventdata, handles)
% hObject    handle to edit23 (see GCBO)
% eventdata  reserved - to be defined in a future version of MATLAB
% handles    structure with handles and user data (see GUIDATA)

% Hints: get(hObject,'String') returns contents of edit23 as text
%        str2double(get(hObject,'String')) returns contents of edit23 as a double
handles.LastElementChanged = handles.edit23; guidata(hObject, handles);
GUI2MatrixRow(hObject, handles);

% --- Executes during object creation, after setting all properties.
function edit23_CreateFcn(hObject, eventdata, handles)
% hObject    handle to edit23 (see GCBO)
% eventdata  reserved - to be defined in a future version of MATLAB
% handles    empty - handles not created until after all CreateFcns called

% Hint: edit controls usually have a white background on Windows.
%       See ISPC and COMPUTER.
if ispc && isequal(get(hObject,'BackgroundColor'), get(0,'defaultUicontrolBackgroundColor'))
    set(hObject,'BackgroundColor','white');
end



function edit24_Callback(hObject, eventdata, handles)
% hObject    handle to edit24 (see GCBO)
% eventdata  reserved - to be defined in a future version of MATLAB
% handles    structure with handles and user data (see GUIDATA)

% Hints: get(hObject,'String') returns contents of edit24 as text
%        str2double(get(hObject,'String')) returns contents of edit24 as a double
handles.LastElementChanged = handles.edit24; guidata(hObject, handles);
GUI2MatrixRow(hObject, handles);
% --- Executes during object creation, after setting all properties.
function edit24_CreateFcn(hObject, eventdata, handles)
% hObject    handle to edit24 (see GCBO)
% eventdata  reserved - to be defined in a future version of MATLAB
% handles    empty - handles not created until after all CreateFcns called

% Hint: edit controls usually have a white background on Windows.
%       See ISPC and COMPUTER.
if ispc && isequal(get(hObject,'BackgroundColor'), get(0,'defaultUicontrolBackgroundColor'))
    set(hObject,'BackgroundColor','white');
end



function edit25_Callback(hObject, eventdata, handles)
% hObject    handle to edit25 (see GCBO)
% eventdata  reserved - to be defined in a future version of MATLAB
% handles    structure with handles and user data (see GUIDATA)

% Hints: get(hObject,'String') returns contents of edit25 as text
%        str2double(get(hObject,'String')) returns contents of edit25 as a double
handles.LastElementChanged = handles.edit25; guidata(hObject, handles);
GUI2MatrixRow(hObject, handles);
% --- Executes during object creation, after setting all properties.
function edit25_CreateFcn(hObject, eventdata, handles)
% hObject    handle to edit25 (see GCBO)
% eventdata  reserved - to be defined in a future version of MATLAB
% handles    empty - handles not created until after all CreateFcns called

% Hint: edit controls usually have a white background on Windows.
%       See ISPC and COMPUTER.
if ispc && isequal(get(hObject,'BackgroundColor'), get(0,'defaultUicontrolBackgroundColor'))
    set(hObject,'BackgroundColor','white');
end



function edit26_Callback(hObject, eventdata, handles)
% hObject    handle to edit26 (see GCBO)
% eventdata  reserved - to be defined in a future version of MATLAB
% handles    structure with handles and user data (see GUIDATA)

% Hints: get(hObject,'String') returns contents of edit26 as text
%        str2double(get(hObject,'String')) returns contents of edit26 as a double
handles.LastElementChanged = handles.edit26; guidata(hObject, handles);
GUI2MatrixRow(hObject, handles);
% --- Executes during object creation, after setting all properties.
function edit26_CreateFcn(hObject, eventdata, handles)
% hObject    handle to edit26 (see GCBO)
% eventdata  reserved - to be defined in a future version of MATLAB
% handles    empty - handles not created until after all CreateFcns called

% Hint: edit controls usually have a white background on Windows.
%       See ISPC and COMPUTER.
if ispc && isequal(get(hObject,'BackgroundColor'), get(0,'defaultUicontrolBackgroundColor'))
    set(hObject,'BackgroundColor','white');
end



function edit27_Callback(hObject, eventdata, handles)
% hObject    handle to edit27 (see GCBO)
% eventdata  reserved - to be defined in a future version of MATLAB
% handles    structure with handles and user data (see GUIDATA)

% Hints: get(hObject,'String') returns contents of edit27 as text
%        str2double(get(hObject,'String')) returns contents of edit27 as a double
handles.LastElementChanged = handles.edit27; guidata(hObject, handles);
GUI2MatrixRow(hObject, handles);

% --- Executes during object creation, after setting all properties.
function edit27_CreateFcn(hObject, eventdata, handles)
% hObject    handle to edit27 (see GCBO)
% eventdata  reserved - to be defined in a future version of MATLAB
% handles    empty - handles not created until after all CreateFcns called

% Hint: edit controls usually have a white background on Windows.
%       See ISPC and COMPUTER.
if ispc && isequal(get(hObject,'BackgroundColor'), get(0,'defaultUicontrolBackgroundColor'))
    set(hObject,'BackgroundColor','white');
end



function edit28_Callback(hObject, eventdata, handles)
% hObject    handle to edit28 (see GCBO)
% eventdata  reserved - to be defined in a future version of MATLAB
% handles    structure with handles and user data (see GUIDATA)

% Hints: get(hObject,'String') returns contents of edit28 as text
%        str2double(get(hObject,'String')) returns contents of edit28 as a double
handles.LastElementChanged = handles.edit28; guidata(hObject, handles);
GUI2MatrixRow(hObject, handles);

% --- Executes during object creation, after setting all properties.
function edit28_CreateFcn(hObject, eventdata, handles)
% hObject    handle to edit28 (see GCBO)
% eventdata  reserved - to be defined in a future version of MATLAB
% handles    empty - handles not created until after all CreateFcns called

% Hint: edit controls usually have a white background on Windows.
%       See ISPC and COMPUTER.
if ispc && isequal(get(hObject,'BackgroundColor'), get(0,'defaultUicontrolBackgroundColor'))
    set(hObject,'BackgroundColor','white');
end



function edit29_Callback(hObject, eventdata, handles)
% hObject    handle to edit29 (see GCBO)
% eventdata  reserved - to be defined in a future version of MATLAB
% handles    structure with handles and user data (see GUIDATA)

% Hints: get(hObject,'String') returns contents of edit29 as text
%        str2double(get(hObject,'String')) returns contents of edit29 as a double
handles.LastElementChanged = handles.edit29; guidata(hObject, handles);
GUI2MatrixRow(hObject, handles);

% --- Executes during object creation, after setting all properties.
function edit29_CreateFcn(hObject, eventdata, handles)
% hObject    handle to edit29 (see GCBO)
% eventdata  reserved - to be defined in a future version of MATLAB
% handles    empty - handles not created until after all CreateFcns called

% Hint: edit controls usually have a white background on Windows.
%       See ISPC and COMPUTER.
if ispc && isequal(get(hObject,'BackgroundColor'), get(0,'defaultUicontrolBackgroundColor'))
    set(hObject,'BackgroundColor','white');
end



function edit30_Callback(hObject, eventdata, handles)
% hObject    handle to edit30 (see GCBO)
% eventdata  reserved - to be defined in a future version of MATLAB
% handles    structure with handles and user data (see GUIDATA)

% Hints: get(hObject,'String') returns contents of edit30 as text
%        str2double(get(hObject,'String')) returns contents of edit30 as a double
handles.LastElementChanged = handles.edit30; guidata(hObject, handles);
GUI2MatrixRow(hObject, handles);

% --- Executes during object creation, after setting all properties.
function edit30_CreateFcn(hObject, eventdata, handles)
% hObject    handle to edit30 (see GCBO)
% eventdata  reserved - to be defined in a future version of MATLAB
% handles    empty - handles not created until after all CreateFcns called

% Hint: edit controls usually have a white background on Windows.
%       See ISPC and COMPUTER.
if ispc && isequal(get(hObject,'BackgroundColor'), get(0,'defaultUicontrolBackgroundColor'))
    set(hObject,'BackgroundColor','white');
end


% --- Executes on button press in pushbutton3.
function pushbutton3_Callback(hObject, eventdata, handles)
% hObject    handle to pushbutton3 (see GCBO)
% eventdata  reserved - to be defined in a future version of MATLAB
% handles    structure with handles and user data (see GUIDATA)


% --- Executes on button press in checkbox6.
function checkbox6_Callback(hObject, eventdata, handles)
% hObject    handle to checkbox6 (see GCBO)
% eventdata  reserved - to be defined in a future version of MATLAB
% handles    structure with handles and user data (see GUIDATA)

% Hint: get(hObject,'Value') returns toggle state of checkbox6
handles.LastElementChanged = handles.checkbox6; guidata(hObject, handles);
GUI2MatrixRow(hObject, handles);

function edit31_Callback(hObject, eventdata, handles)
% hObject    handle to edit31 (see GCBO)
% eventdata  reserved - to be defined in a future version of MATLAB
% handles    structure with handles and user data (see GUIDATA)

% Hints: get(hObject,'String') returns contents of edit31 as text
%        str2double(get(hObject,'String')) returns contents of edit31 as a double

% --- Executes during object creation, after setting all properties.
function edit31_CreateFcn(hObject, eventdata, handles)
% hObject    handle to edit31 (see GCBO)
% eventdata  reserved - to be defined in a future version of MATLAB
% handles    empty - handles not created until after all CreateFcns called

% Hint: edit controls usually have a white background on Windows.
%       See ISPC and COMPUTER.
if ispc && isequal(get(hObject,'BackgroundColor'), get(0,'defaultUicontrolBackgroundColor'))
    set(hObject,'BackgroundColor','white');
end



function edit32_Callback(hObject, eventdata, handles)
% hObject    handle to edit32 (see GCBO)
% eventdata  reserved - to be defined in a future version of MATLAB
% handles    structure with handles and user data (see GUIDATA)

% Hints: get(hObject,'String') returns contents of edit32 as text
%        str2double(get(hObject,'String')) returns contents of edit32 as a double

% --- Executes during object creation, after setting all properties.
function edit32_CreateFcn(hObject, eventdata, handles)
% hObject    handle to edit32 (see GCBO)
% eventdata  reserved - to be defined in a future version of MATLAB
% handles    empty - handles not created until after all CreateFcns called

% Hint: edit controls usually have a white background on Windows.
%       See ISPC and COMPUTER.
if ispc && isequal(get(hObject,'BackgroundColor'), get(0,'defaultUicontrolBackgroundColor'))
    set(hObject,'BackgroundColor','white');
end



function edit33_Callback(hObject, eventdata, handles)
% hObject    handle to edit33 (see GCBO)
% eventdata  reserved - to be defined in a future version of MATLAB
% handles    structure with handles and user data (see GUIDATA)

% Hints: get(hObject,'String') returns contents of edit33 as text
%        str2double(get(hObject,'String')) returns contents of edit33 as a double


% --- Executes during object creation, after setting all properties.
function edit33_CreateFcn(hObject, eventdata, handles)
% hObject    handle to edit33 (see GCBO)
% eventdata  reserved - to be defined in a future version of MATLAB
% handles    empty - handles not created until after all CreateFcns called

% Hint: edit controls usually have a white background on Windows.
%       See ISPC and COMPUTER.
if ispc && isequal(get(hObject,'BackgroundColor'), get(0,'defaultUicontrolBackgroundColor'))
    set(hObject,'BackgroundColor','white');
end


% --- Executes on button press in pushbutton4.
function pushbutton4_Callback(hObject, eventdata, handles)
% hObject    handle to pushbutton4 (see GCBO)
% eventdata  reserved - to be defined in a future version of MATLAB
% handles    structure with handles and user data (see GUIDATA)


function GUI2MatrixRow(hObject, handles)
% Read matrix row into all gui fields
OutputChannels = get(handles.listbox1, 'Value') + 1;
TriggerChannels = get(handles.listbox2, 'Value') + 7;
Matrix = handles.Matrix;
UpdateType = 0; % 0 if output channels, 1 if input channels
nOutputChannelsSelected = length(OutputChannels);
nTriggerChannelsSelected = length(TriggerChannels);
CurrentHandle = handles.LastElementChanged;
Value2Copy = [];
Row2Update = [];
OldMatrix = Matrix;
if ~isempty(CurrentHandle) % If an element has changed
    switch CurrentHandle
        % Output Channels
        case handles.edit29
            Value2Copy = str2double(get(handles.edit29, 'String')); Row2Update = 3; UpdateType = 0;
        case handles.edit30
            Value2Copy = str2double(get(handles.edit30, 'String')); Row2Update = 4; UpdateType = 0;
        case handles.edit21
            Value2Copy = str2double(get(handles.edit21, 'String')); Row2Update = 5; UpdateType = 0;
        case handles.edit22
            Value2Copy = str2double(get(handles.edit22, 'String')); Row2Update = 6; UpdateType = 0;
        case handles.edit23
            Value2Copy = str2double(get(handles.edit23, 'String')); Row2Update = 7; UpdateType = 0;
        case handles.edit24
            Value2Copy = str2double(get(handles.edit24, 'String')); Row2Update = 8; UpdateType = 0;
        case handles.edit25
            Value2Copy = str2double(get(handles.edit25, 'String')); Row2Update = 9; UpdateType = 0;
        case handles.edit26
            Value2Copy = str2double(get(handles.edit26, 'String')); Row2Update = 10; UpdateType = 0;
        case handles.edit27
            Value2Copy = str2double(get(handles.edit27, 'String')); Row2Update = 11; UpdateType = 0;
        case handles.edit28
            Value2Copy = str2double(get(handles.edit28, 'String')); Row2Update = 12; UpdateType = 0;
        case handles.edit37
            Value2Copy = str2double(get(handles.edit37, 'String')); Row2Update = 18; UpdateType = 0;
        case handles.popupmenu1
            Value2Copy = (get(handles.popupmenu1, 'Value') - 1); Row2Update = 2; UpdateType = 0;
        case handles.checkbox1
            Value2Copy = get(handles.checkbox1, 'Value'); Row2Update = 13; UpdateType = 0;
        case handles.checkbox2
            Value2Copy = get(handles.checkbox2, 'Value'); Row2Update = 14; UpdateType = 0;
        case handles.checkbox7
            Value2Copy = get(handles.checkbox7, 'Value'); Row2Update = 17; UpdateType = 0;
        case handles.popupmenu2
            Value2Copy = (get(handles.popupmenu2, 'Value') - 1); Row2Update = 15; UpdateType = 0;
        case handles.popupmenu3
            Value2Copy = (get(handles.popupmenu3, 'Value') - 1); Row2Update = 16; UpdateType = 0;
        % Input Channels
        case handles.popupmenu4
            Value2Copy = (get(handles.popupmenu4, 'Value') - 1); Row2Update = 2; UpdateType = 1;
    end

    % Update Matrix
    if UpdateType == 0
        for x = 1:nOutputChannelsSelected
            Matrix{Row2Update,OutputChannels(x)} = Value2Copy;
        end
    else
        for x = 1:nTriggerChannelsSelected
            Matrix{Row2Update,TriggerChannels(x)} = Value2Copy;
        end
    end
end

handles.Matrix = Matrix;
guidata(hObject,handles);
MatrixRow2GUI(hObject, handles);


function MatrixRow2GUI(hObject, handles)
OutputChannels = get(handles.listbox1, 'Value') + 1;
TriggerChannels = get(handles.listbox2, 'Value') + 7;
Matrix = handles.Matrix;
if length(OutputChannels) == 1 % Otherwise, multiple select mode - do not update outputs
    set(handles.popupmenu1, 'Value', Matrix{2,OutputChannels} + 1);
    set(handles.popupmenu2, 'Value', Matrix{15,OutputChannels} + 1);
    set(handles.popupmenu3, 'Value', Matrix{16,OutputChannels} + 1);
    set(handles.checkbox1, 'Value', Matrix{13, OutputChannels});
    set(handles.checkbox2, 'Value', Matrix{14, OutputChannels});
    set(handles.edit30, 'String', num2str(Matrix{4, OutputChannels}));
    set(handles.edit29, 'String', num2str(Matrix{3, OutputChannels}));
    set(handles.edit21, 'String', num2str(Matrix{5, OutputChannels}));
    set(handles.edit22, 'String', num2str(Matrix{6, OutputChannels}));
    set(handles.edit23, 'String', num2str(Matrix{7, OutputChannels}));
    set(handles.edit24, 'String', num2str(Matrix{8, OutputChannels}));
    set(handles.edit25, 'String', num2str(Matrix{9, OutputChannels}));
    set(handles.edit26, 'String', num2str(Matrix{10, OutputChannels}));
    set(handles.edit27, 'String', num2str(Matrix{11, OutputChannels}));
    set(handles.edit28, 'String', num2str(Matrix{12, OutputChannels}));
    set(handles.checkbox7, 'Value', Matrix{17, OutputChannels});
    set(handles.edit37, 'String', num2str(Matrix{18, OutputChannels}));
end
if length(TriggerChannels) == 1 % Otherwise, multiple select mode - do not update outputs
    set(handles.popupmenu4, 'Value', Matrix{2, TriggerChannels} + 1);
end

% Enable and disable boxes
Value = (get(handles.popupmenu2, 'Value') - 1);
switch Value
    case 0
        set(handles.popupmenu3, 'Enable', 'off');
        set(handles.edit11, 'Enable', 'off');
        set(handles.edit31, 'Enable', 'off');
        set(handles.edit32, 'Enable', 'off');
        set(handles.edit33, 'Enable', 'off');
        set(handles.checkbox7, 'Enable', 'off');
    otherwise
        set(handles.popupmenu3, 'Enable', 'on');
        set(handles.edit11, 'Enable', 'on');
        set(handles.edit31, 'Enable', 'on');
        set(handles.edit32, 'Enable', 'on');
        set(handles.edit33, 'Enable', 'on');
        set(handles.checkbox7, 'Enable', 'on');
end
Value = (get(handles.popupmenu1, 'Value') - 1);
switch Value
    case 0
        set(handles.edit22, 'Enable', 'off');
        set(handles.edit23, 'Enable', 'off');
    case 1
        set(handles.edit22, 'Enable', 'on');
        set(handles.edit23, 'Enable', 'on');
end
guidata(hObject,handles);
function edit34_Callback(hObject, eventdata, handles)
% hObject    handle to edit34 (see GCBO)
% eventdata  reserved - to be defined in a future version of MATLAB
% handles    structure with handles and user data (see GUIDATA)

% Hints: get(hObject,'String') returns contents of edit34 as text
%        str2double(get(hObject,'String')) returns contents of edit34 as a double
handles.LastElementChanged = handles.edit34; guidata(hObject, handles);
GUI2MatrixRow(hObject, handles);

% --- Executes during object creation, after setting all properties.
function edit34_CreateFcn(hObject, eventdata, handles)
% hObject    handle to edit34 (see GCBO)
% eventdata  reserved - to be defined in a future version of MATLAB
% handles    empty - handles not created until after all CreateFcns called

% Hint: edit controls usually have a white background on Windows.
%       See ISPC and COMPUTER.
if ispc && isequal(get(hObject,'BackgroundColor'), get(0,'defaultUicontrolBackgroundColor'))
    set(hObject,'BackgroundColor','white');
end



function edit35_Callback(hObject, eventdata, handles)
% hObject    handle to edit35 (see GCBO)
% eventdata  reserved - to be defined in a future version of MATLAB
% handles    structure with handles and user data (see GUIDATA)

% Hints: get(hObject,'String') returns contents of edit35 as text
%        str2double(get(hObject,'String')) returns contents of edit35 as a double
handles.LastElementChanged = handles.edit35; guidata(hObject, handles);
GUI2MatrixRow(hObject, handles);

% --- Executes during object creation, after setting all properties.
function edit35_CreateFcn(hObject, eventdata, handles)
% hObject    handle to edit35 (see GCBO)
% eventdata  reserved - to be defined in a future version of MATLAB
% handles    empty - handles not created until after all CreateFcns called

% Hint: edit controls usually have a white background on Windows.
%       See ISPC and COMPUTER.
if ispc && isequal(get(hObject,'BackgroundColor'), get(0,'defaultUicontrolBackgroundColor'))
    set(hObject,'BackgroundColor','white');
end


% --- Executes on button press in pushbutton5.
function pushbutton5_Callback(hObject, eventdata, handles)
% hObject    handle to pushbutton5 (see GCBO)
% eventdata  reserved - to be defined in a future version of MATLAB
% handles    structure with handles and user data (see GUIDATA)

% define the URL for US Naval Observatory Time page (to check for Internet
% connectivity)
url =java.net.URL('http://tycho.usno.navy.mil/cgi-bin/timer.pl');

% read the test-URL to confirm connectivity
try
    link = openStream(url);
    parse = java.io.InputStreamReader(link);
    snip = java.io.BufferedReader(parse);
    if ~isempty(snip)
        system('start https://sites.google.com/site/pulsepalwiki/parameter-guide'); % Launch help site
    else
        msgbox('Error: Internet connectivity is required for help documentation. See command window for url.')
        disp('An illustrated parameter guide is available at: https://sites.google.com/site/pulsepalwiki/parameter-guide')
    end
catch
    msgbox('Error: Internet connectivity is required for help documentation. See command window for url.')
    disp('An illustrated parameter guide is available at: https://sites.google.com/site/pulsepalwiki/parameter-guide')
end

    


% --- Executes on button press in checkbox7.
function checkbox7_Callback(hObject, eventdata, handles)
% hObject    handle to checkbox7 (see GCBO)
% eventdata  reserved - to be defined in a future version of MATLAB
% handles    structure with handles and user data (see GUIDATA)

% Hint: get(hObject,'Value') returns toggle state of checkbox7
handles.LastElementChanged = handles.checkbox7; guidata(hObject, handles);
GUI2MatrixRow(hObject, handles);


function edit36_Callback(hObject, eventdata, handles)
% hObject    handle to edit36 (see GCBO)
% eventdata  reserved - to be defined in a future version of MATLAB
% handles    structure with handles and user data (see GUIDATA)

% Hints: get(hObject,'String') returns contents of edit36 as text
%        str2double(get(hObject,'String')) returns contents of edit36 as a double


% --- Executes during object creation, after setting all properties.
function edit36_CreateFcn(hObject, eventdata, handles)
% hObject    handle to edit36 (see GCBO)
% eventdata  reserved - to be defined in a future version of MATLAB
% handles    empty - handles not created until after all CreateFcns called

% Hint: edit controls usually have a white background on Windows.
%       See ISPC and COMPUTER.
if ispc && isequal(get(hObject,'BackgroundColor'), get(0,'defaultUicontrolBackgroundColor'))
    set(hObject,'BackgroundColor','white');
end


% --- Executes on button press in pushbutton6.
function pushbutton6_Callback(hObject, eventdata, handles)
% hObject    handle to pushbutton6 (see GCBO)
% eventdata  reserved - to be defined in a future version of MATLAB
% handles    structure with handles and user data (see GUIDATA)


% --------------------------------------------------------------------
function uipushtool4_ClickedCallback(hObject, eventdata, handles)
% hObject    handle to uipushtool4 (see GCBO)
% eventdata  reserved - to be defined in a future version of MATLAB
% handles    structure with handles and user data (see GUIDATA)
Matrix = handles.Matrix;
    ValidStim = 1; 
    SendTrain1 = 0;
    SendTrain2 = 0;
    ErrorDetected = 0;
    Stim1Times = get(handles.edit11, 'String');
    Stim1Voltages = get(handles.edit31, 'String');
    Stim2Times = get(handles.edit32, 'String');
    Stim2Voltages = get(handles.edit33, 'String');
    if ~isempty(Stim1Times) && ~isempty(Stim1Voltages)
        if ~(strcmp(Stim1Times, 'None') && strcmp(Stim1Voltages, 'None'))
            OriginalTimes = str2num(Stim1Times);
            CandidateTimes = str2num(Stim1Times)*1000000;
            CandidateVoltages = str2num(Stim1Voltages);
            if isempty(CandidateTimes)
                ValidStim = 0; PulsePalErrorMsg('Error: Custom timestamps must be numbers');
            end
            if isempty(CandidateVoltages) && ValidStim == 1
                ValidStim = 0; PulsePalErrorMsg('Error: Custom voltages must be numbers');
            end
            if sum((CandidateTimes < 0) > 0) && ValidStim == 1
                ValidStim = 0; PulsePalErrorMsg('Error: Custom timestamps must be positive');
            end
            if (length(unique(CandidateTimes)) ~= length(CandidateTimes))  && ValidStim == 1
                ValidStim = 0; PulsePalErrorMsg('Error: Duplicate custom timestamps detected');
            end
            if ~IsTimeSequence(CandidateTimes) && ValidStim == 1
                ValidStim = 0; PulsePalErrorMsg('Error: Custom timestamps must always increase');
            end
            if (sum(rem(CandidateTimes,50)) > 0)  && ValidStim == 1
                ValidStim = 0; PulsePalErrorMsg('Error: Custom timestamps must be multiples of 0.00005 seconds');
            end
            if (CandidateTimes(length(CandidateTimes)) > 3600000000)  && ValidStim == 1
                ValidStim = 0; PulsePalErrorMsg('Error: Custom timestamps must be < 3600 s');
            end
            if (sum(abs(CandidateVoltages) > 10) > 0)  && ValidStim == 1
                ValidStim = 0; PulsePalErrorMsg('Error: Custom voltage range = -10V to +10V');
            end
            if (length(CandidateVoltages) ~= length(CandidateTimes)) && ValidStim == 1
                ValidStim = 0; PulsePalErrorMsg('Error: There must be a voltage for every timestamp');
            end
            Stim1Times = OriginalTimes;
            Stim1Voltages = CandidateVoltages;
            SendTrain1 = 1; 
        end
    end
    if  ~isempty(Stim2Times) && ~isempty(Stim2Voltages)
        if ~(strcmp(Stim2Times, 'None') && strcmp(Stim2Voltages, 'None'))
            OriginalTimes = str2num(Stim2Times);
            CandidateTimes = OriginalTimes*1000000;
            CandidateVoltages = str2num(Stim2Voltages);
            if isempty(CandidateTimes) && ValidStim == 1
                ValidStim = 0; PulsePalErrorMsg('Error: Custom timestamps must be numbers');
            end
            if isempty(CandidateVoltages) && ValidStim == 1
                ValidStim = 0; PulsePalErrorMsg('Error: Custom voltages must be numbers');
            end
            if (sum(CandidateTimes < 0) > 0)  && ValidStim == 1
                ValidStim = 0; PulsePalErrorMsg('Error: Custom timestamps must be positive');
            end
            if (length(unique(CandidateTimes)) ~= length(CandidateTimes)) && ValidStim == 1
                ValidStim = 0; PulsePalErrorMsg('Error: Duplicate custom timestamps detected');
            end
            if ~IsTimeSequence(CandidateTimes)  && ValidStim == 1
                ValidStim = 0; PulsePalErrorMsg('Error: Custom timestamps must always increase');
            end
            if (sum(rem(CandidateTimes,50)) > 0)  && ValidStim == 1
                ValidStim = 0; PulsePalErrorMsg('Error: Custom timestamps must be multiples of 0.00005 seconds');
            end
            if (CandidateTimes(length(CandidateTimes)) > 3600000000) && ValidStim == 1
                ValidStim = 0; PulsePalErrorMsg('Error: Custom timestamps must be < 3600 s');
            end
            if (sum(abs(CandidateVoltages) > 10) > 0) && ValidStim == 1
                ValidStim = 0; PulsePalErrorMsg('Error: Custom voltage range = -10V to +10V');
            end
            if (length(CandidateVoltages) ~= length(CandidateTimes)) && ValidStim == 1
                ValidStim = 0; PulsePalErrorMsg('Error: There must be a voltage for every timestamp');
            end
            Stim2Times = OriginalTimes;
            Stim2Voltages = CandidateVoltages;
            SendTrain2 = 1;
        end
    end
    if ValidStim == 1
        ProgramPulsePal(Matrix);
        if SendTrain1 == 1
            pause(.01);
            SendCustomPulseTrain(1, Stim1Times, Stim1Voltages);
        end
        if SendTrain2 == 1
            pause(.01);
            SendCustomPulseTrain(2, Stim2Times, Stim2Voltages);
        end
    end


% --------------------------------------------------------------------
function uipushtool3_ClickedCallback(hObject, eventdata, handles)
% hObject    handle to uipushtool3 (see GCBO)
% eventdata  reserved - to be defined in a future version of MATLAB
% handles    structure with handles and user data (see GUIDATA)
ParameterMatrix = handles.Matrix;
[file,path] = uiputfile('PulsePalProgram.mat','Save program');
Savepath = fullfile(path, file);
save(Savepath, 'ParameterMatrix');


% --------------------------------------------------------------------
function uipushtool1_ClickedCallback(hObject, eventdata, handles)
% hObject    handle to uipushtool1 (see GCBO)
% eventdata  reserved - to be defined in a future version of MATLAB
% handles    structure with handles and user data (see GUIDATA)
global PulsePalSystem
TemplatePath = fullfile(PulsePalSystem.PulsePalPath,'GUI','PulsePalProgram_Template.mat');
load(TemplatePath);
handles.Matrix = ParameterMatrix;
set(handles.listbox1, 'Value', 1); set(handles.listbox2, 'Value', 1);
MatrixRow2GUI(hObject, handles);


% --------------------------------------------------------------------
function uipushtool2_ClickedCallback(hObject, eventdata, handles)
% hObject    handle to uipushtool2 (see GCBO)
% eventdata  reserved - to be defined in a future version of MATLAB
% handles    structure with handles and user data (see GUIDATA)
[file,path] = uigetfile('*.mat','Load program');
Loadpath = fullfile(path, file);
load(Loadpath);
handles.Matrix = ParameterMatrix;
set(handles.listbox1, 'Value', 1); set(handles.listbox2, 'Value', 1);
MatrixRow2GUI(hObject, handles);


% --- Executes on button press in checkbox8.
function checkbox8_Callback(hObject, eventdata, handles)
% hObject    handle to checkbox8 (see GCBO)
% eventdata  reserved - to be defined in a future version of MATLAB
% handles    structure with handles and user data (see GUIDATA)

% Hint: get(hObject,'Value') returns toggle state of checkbox8


% --- Executes on button press in checkbox9.
function checkbox9_Callback(hObject, eventdata, handles)
% hObject    handle to checkbox9 (see GCBO)
% eventdata  reserved - to be defined in a future version of MATLAB
% handles    structure with handles and user data (see GUIDATA)

% Hint: get(hObject,'Value') returns toggle state of checkbox9


% --- Executes on button press in checkbox10.
function checkbox10_Callback(hObject, eventdata, handles)
% hObject    handle to checkbox10 (see GCBO)
% eventdata  reserved - to be defined in a future version of MATLAB
% handles    structure with handles and user data (see GUIDATA)

% Hint: get(hObject,'Value') returns toggle state of checkbox10


% --- Executes on button press in checkbox11.
function checkbox11_Callback(hObject, eventdata, handles)
% hObject    handle to checkbox11 (see GCBO)
% eventdata  reserved - to be defined in a future version of MATLAB
% handles    structure with handles and user data (see GUIDATA)

% Hint: get(hObject,'Value') returns toggle state of checkbox11


% --- Executes on button press in pushbutton7.
function pushbutton7_Callback(hObject, eventdata, handles)
% hObject    handle to pushbutton7 (see GCBO)
% eventdata  reserved - to be defined in a future version of MATLAB
% handles    structure with handles and user data (see GUIDATA)
global PulsePalSystem;
set(handles.pushbutton7, 'CData', handles.StimButtonGFX_Pressed);
drawnow;
Targets = zeros(1,4);
Targets(1) = get(handles.checkbox8, 'Value');
Targets(2) = get(handles.checkbox9, 'Value');
Targets(3) = get(handles.checkbox10, 'Value');
Targets(4) = get(handles.checkbox11, 'Value');
Targets = Targets(end:-1:1);
TargetString = [num2str(Targets(1)) num2str(Targets(2)) num2str(Targets(3)) num2str(Targets(4))];
TriggerPulsePal(TargetString)
pause(.2);
set(handles.pushbutton7, 'CData', handles.StimButtonGFX_Unpressed);



function edit37_Callback(hObject, eventdata, handles)
% hObject    handle to edit37 (see GCBO)
% eventdata  reserved - to be defined in a future version of MATLAB
% handles    structure with handles and user data (see GUIDATA)

% Hints: get(hObject,'String') returns contents of edit37 as text
%        str2double(get(hObject,'String')) returns contents of edit37 as a double
handles.LastElementChanged = handles.edit37; guidata(hObject, handles);
GUI2MatrixRow(hObject, handles);

% --- Executes during object creation, after setting all properties.
function edit37_CreateFcn(hObject, eventdata, handles)
% hObject    handle to edit37 (see GCBO)
% eventdata  reserved - to be defined in a future version of MATLAB
% handles    empty - handles not created until after all CreateFcns called

% Hint: edit controls usually have a white background on Windows.
%       See ISPC and COMPUTER.
if ispc && isequal(get(hObject,'BackgroundColor'), get(0,'defaultUicontrolBackgroundColor'))
    set(hObject,'BackgroundColor','white');
end
