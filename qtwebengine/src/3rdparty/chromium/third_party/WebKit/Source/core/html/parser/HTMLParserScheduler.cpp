/*
 * Copyright (C) 2010 Google, Inc. All Rights Reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "core/html/parser/HTMLParserScheduler.h"

#include "core/dom/Document.h"
#include "core/html/parser/HTMLDocumentParser.h"
#include "core/frame/FrameView.h"
#include "platform/scheduler/Scheduler.h"
#include "wtf/CurrentTime.h"

namespace blink {

ActiveParserSession::ActiveParserSession(Document* document)
    : m_document(document)
{
    if (!m_document)
        return;
    m_document->incrementActiveParserCount();
}

ActiveParserSession::~ActiveParserSession()
{
    if (!m_document)
        return;
    m_document->decrementActiveParserCount();
}

PumpSession::PumpSession(unsigned& nestingLevel, Document* document)
    : NestingLevelIncrementer(nestingLevel)
    , ActiveParserSession(document)
{
}

PumpSession::~PumpSession()
{
}

SpeculationsPumpSession::SpeculationsPumpSession(Document* document)
    : ActiveParserSession(document)
    , m_startTime(currentTime())
{
}

SpeculationsPumpSession::~SpeculationsPumpSession()
{
}

inline double SpeculationsPumpSession::elapsedTime() const
{
    return currentTime() - m_startTime;
}

HTMLParserScheduler::HTMLParserScheduler(HTMLDocumentParser* parser)
    : m_parser(parser)
    , m_continueNextChunkTimer(this, &HTMLParserScheduler::continueNextChunkTimerFired)
    , m_isSuspendedWithActiveTimer(false)
{
}

HTMLParserScheduler::~HTMLParserScheduler()
{
    m_continueNextChunkTimer.stop();
}

void HTMLParserScheduler::continueNextChunkTimerFired(Timer<HTMLParserScheduler>* timer)
{
    ASSERT_UNUSED(timer, timer == &m_continueNextChunkTimer);
    m_parser->resumeParsingAfterYield();
}

void HTMLParserScheduler::scheduleForResume()
{
    m_continueNextChunkTimer.startOneShot(0, FROM_HERE);
}

void HTMLParserScheduler::suspend()
{
    ASSERT(!m_isSuspendedWithActiveTimer);
    if (!m_continueNextChunkTimer.isActive())
        return;
    m_isSuspendedWithActiveTimer = true;
    m_continueNextChunkTimer.stop();
}

void HTMLParserScheduler::resume()
{
    ASSERT(!m_continueNextChunkTimer.isActive());
    if (!m_isSuspendedWithActiveTimer)
        return;
    m_isSuspendedWithActiveTimer = false;
    m_continueNextChunkTimer.startOneShot(0, FROM_HERE);
}

inline bool HTMLParserScheduler::shouldYield(const SpeculationsPumpSession& session) const
{
    if (Scheduler::shared()->shouldYieldForHighPriorityWork())
        return true;

    const double parserTimeLimit = 0.5;
    if (session.elapsedTime() > parserTimeLimit)
        return true;

    return false;
}

bool HTMLParserScheduler::yieldIfNeeded(const SpeculationsPumpSession& session)
{
    if (shouldYield(session)) {
        scheduleForResume();
        return true;
    }

    return false;
}

}