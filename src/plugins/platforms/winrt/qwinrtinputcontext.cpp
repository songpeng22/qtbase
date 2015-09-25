/****************************************************************************
**
** Copyright (C) 2015 The Qt Company Ltd.
** Contact: http://www.qt.io/licensing/
**
** This file is part of the plugins of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL3$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see http://www.qt.io/terms-conditions. For further
** information use the contact form at http://www.qt.io/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 3 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPLv3 included in the
** packaging of this file. Please review the following information to
** ensure the GNU Lesser General Public License version 3 requirements
** will be met: https://www.gnu.org/licenses/lgpl.html.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 2.0 or later as published by the Free
** Software Foundation and appearing in the file LICENSE.GPL included in
** the packaging of this file. Please review the following information to
** ensure the GNU General Public License version 2.0 requirements will be
** met: http://www.gnu.org/licenses/gpl-2.0.html.
**
** $QT_END_LICENSE$
**
****************************************************************************/

#include "qwinrtinputcontext.h"
#include "qwinrtscreen.h"
#include <QtGui/QWindow>
#include <private/qeventdispatcher_winrt_p.h>

#include <functional>
#include <wrl.h>
#include <roapi.h>
#include <windows.ui.viewmanagement.h>
#include <windows.ui.core.h>
using namespace Microsoft::WRL;
using namespace Microsoft::WRL::Wrappers;
using namespace ABI::Windows::Foundation;
using namespace ABI::Windows::UI::ViewManagement;
using namespace ABI::Windows::UI::Core;

typedef ITypedEventHandler<InputPane*, InputPaneVisibilityEventArgs*> InputPaneVisibilityHandler;

QT_BEGIN_NAMESPACE

/*!
    \class QWinRTInputContext
    \brief Manages Input Method visibility
    \internal
    \ingroup qt-qpa-winrt

    Listens to the native virtual keyboard for hide/show events and provides
    hints to the OS for showing/hiding. On WinRT, showInputPanel()/hideInputPanel()
    have no effect because WinRT dictates that keyboard presence is user-driven:
    (http://msdn.microsoft.com/en-us/library/windows/apps/hh465404.aspx)
    Windows Phone, however, supports direct hiding/showing of the keyboard.
*/

QWinRTInputContext::QWinRTInputContext(QWinRTScreen *screen)
    : m_screen(screen)
{
    IInputPaneStatics *statics;
    if (FAILED(GetActivationFactory(HString::MakeReference(RuntimeClass_Windows_UI_ViewManagement_InputPane).Get(),
                                    &statics))) {
        qWarning(Q_FUNC_INFO ": failed to retrieve input pane statics.");
        return;
    }

    IInputPane *inputPane;
    statics->GetForCurrentView(&inputPane);
    statics->Release();
    if (inputPane) {
        EventRegistrationToken showToken, hideToken;
        inputPane->add_Showing(Callback<InputPaneVisibilityHandler>(
                                   this, &QWinRTInputContext::onShowing).Get(), &showToken);
        inputPane->add_Hiding(Callback<InputPaneVisibilityHandler>(
                                  this, &QWinRTInputContext::onHiding).Get(), &hideToken);

        handleVisibilityChange(inputPane);
        m_isInputPanelVisible = !m_keyboardRect.isEmpty();
    } else {
        qWarning(Q_FUNC_INFO ": failed to retrieve InputPane.");
    }
}

QRectF QWinRTInputContext::keyboardRect() const
{
    return m_keyboardRect;
}

bool QWinRTInputContext::isInputPanelVisible() const
{
    return m_isInputPanelVisible;
}

HRESULT QWinRTInputContext::onShowing(IInputPane *pane, IInputPaneVisibilityEventArgs *)
{
    m_isInputPanelVisible = true;
    emitInputPanelVisibleChanged();
    return handleVisibilityChange(pane);
}

HRESULT QWinRTInputContext::onHiding(IInputPane *pane, IInputPaneVisibilityEventArgs *)
{
    m_isInputPanelVisible = false;
    emitInputPanelVisibleChanged();
    return handleVisibilityChange(pane);
}

HRESULT QWinRTInputContext::handleVisibilityChange(IInputPane *pane)
{
    Rect rect;
    pane->get_OccludedRect(&rect);
    const QRectF keyboardRect = QRectF(qRound(rect.X * m_screen->scaleFactor()), qRound(rect.Y * m_screen->scaleFactor()),
                                       qRound(rect.Width * m_screen->scaleFactor()), qRound(rect.Height * m_screen->scaleFactor()));
    if (m_keyboardRect != keyboardRect) {
        m_keyboardRect = keyboardRect;
        emitKeyboardRectChanged();
    }
    return S_OK;
}

#ifdef Q_OS_WINPHONE

static HRESULT getInputPane(ComPtr<IInputPane2> *inputPane2)
{
    ComPtr<IInputPaneStatics> factory;
    HRESULT hr = GetActivationFactory(HString::MakeReference(RuntimeClass_Windows_UI_ViewManagement_InputPane).Get(),
                                      &factory);
    if (FAILED(hr)) {
        qErrnoWarning(hr, "Failed to get input pane factory.");
        return hr;
    }

    ComPtr<IInputPane> inputPane;
    hr = factory->GetForCurrentView(&inputPane);
    if (FAILED(hr)) {
        qErrnoWarning(hr, "Failed to get input pane.");
        return hr;
    }

    hr = inputPane.As(inputPane2);
    if (FAILED(hr)) {
        qErrnoWarning(hr, "Failed to get extended input pane.");
        return hr;
    }
    return hr;
}

void QWinRTInputContext::showInputPanel()
{
    ComPtr<IInputPane2> inputPane;
    HRESULT hr = getInputPane(&inputPane);
    if (FAILED(hr))
        return;

    QEventDispatcherWinRT::runOnXamlThread([&inputPane]() {
        HRESULT hr;
        boolean success;
        hr = inputPane->TryShow(&success);
        if (FAILED(hr) || !success)
            qErrnoWarning(hr, "Failed to show input panel.");
        return hr;
    });
}

void QWinRTInputContext::hideInputPanel()
{
    ComPtr<IInputPane2> inputPane;
    HRESULT hr = getInputPane(&inputPane);
    if (FAILED(hr))
        return;

    QEventDispatcherWinRT::runOnXamlThread([&inputPane]() {
        HRESULT hr;
        boolean success;
        hr = inputPane->TryHide(&success);
        if (FAILED(hr) || !success)
            qErrnoWarning(hr, "Failed to hide input panel.");
        return hr;
    });
}

#endif // Q_OS_WINPHONE

QT_END_NAMESPACE
