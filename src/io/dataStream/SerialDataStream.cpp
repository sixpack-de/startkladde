#include "src/io/dataStream/SerialDataStream.h"

#include <QDebug>
#include <QMutex>
#include <QMutexLocker>

#include "3rdparty/qserialdevice/src/qserialdevice/abstractserial.h"

#include "src/text.h"
#include "src/io/serial/SerialPortList.h"
#include "src/i18n/notr.h"

/*
 * Note: this class is thread safe. Keep it that way.
 */

// ******************
// ** Construction **
// ******************

SerialDataStream::SerialDataStream (QObject *parent): DataStream (parent),
	_parameterMutex (new QMutex (QMutex::NonRecursive)),
	_backEndMutex   (new QMutex (QMutex::NonRecursive)),
	_baudRate (0)
{
	connect (SerialPortList::instance (), SIGNAL (portAdded (QString)),
	         this                       , SLOT   (portAdded (QString)));
	connect (SerialPortList::instance (), SIGNAL (portRemoved (QString)),
	         this                       , SLOT   (portRemoved (QString)));

	// Create the port and connect the required signals. _port will be deleted
	// automatically by its parent.
	_port=new AbstractSerial (this);
	_port->enableEmitStatus (true);
	connect (_port, SIGNAL (readyRead         ()),
	         this , SLOT   (port_dataReceived ()));
	connect (_port, SIGNAL (signalStatus (const QString &, QDateTime)),
	         this , SLOT   (port_status  (const QString &, QDateTime)));
}

SerialDataStream::~SerialDataStream ()
{
	delete _parameterMutex;
	delete _backEndMutex;
}


// ****************
// ** Parameters **
// ****************

/**
 * Sets the port and baud rate to use when the stream is opened the next time.
 *
 * This method is thread safe.
 */
void SerialDataStream::setPort (const QString &portName, int baudRate)
{
	QMutexLocker parameterLocker (_parameterMutex);

	_baudRate=baudRate;
	_portName=portName;
}


// ************************
// ** DataStream methods **
// ************************

/**
 * Implementation of DataStream::openStream ().
 *
 * This method is thread safe.
 */
void SerialDataStream::openStream ()
{
	// Lock the parameter mutex and make a copy of the parameters. Unlock the
	// parameter mutex afterwards.
	QMutexLocker parameterLocker (_parameterMutex);
	QString portName=_portName;
	int baudRate=_baudRate;
	parameterLocker.unlock ();

	if (isBlank (portName))
	{
		streamError (tr ("No port specified"));
		return;
	}

	// Lock the back-end mutex and open the port. Unlock the mutex before
	// calling the base class. It will also be unlocked when the method returns.
	QMutexLocker backEndLocker (_backEndMutex);

	// If the port does not exist, the error message generated by the
	// QSerialDevice library is not very helpful. Create an own error message
	// instead.
	if (!SerialPortList::instance ()->isPortAvailable (portName, Qt::CaseInsensitive))
	{
		backEndLocker.unlock ();
		streamError (tr ("The port %1 does not exist").arg (portName));
		return;
	}

	// Open the port and check success.
	_port->setDeviceName (portName);
	_port->open (QIODevice::ReadOnly);
	if (_port->isOpen ())
	{
		// Configure the port. The port is currently configured as the last user of
		// the port left it.
		_port->setBaudRate    (baudRate);
		_port->setDataBits    (AbstractSerial::DataBits8);
		_port->setParity      (AbstractSerial::ParityNone);
		_port->setStopBits    (AbstractSerial::StopBits1);
		_port->setFlowControl (AbstractSerial::FlowControlOff);
		_port->setDtr         (true);
		_port->setRts         (true);

		backEndLocker.unlock ();
		streamOpened ();
	}
	else
	{
		// The (modified) QSerialDevice library now provides a more useful error
		// message. Use this message as the stream's error message.
		QString errorMessage=_port->errorString ();
		backEndLocker.unlock ();
		//streamError (tr ("Connection did not open"));
		streamError (errorMessage);
	}
}

/**
 * Implementation of DataStream::closeStream ().
 *
 * This method is thread safe.
 */
void SerialDataStream::closeStream ()
{
	// Lock the back-end mutex and close the port.
	QMutexLocker backEndLocker (_backEndMutex);

	_port->close ();
}

// Adapted from QSerialDevice's abstractserial.cpp
static int baudRateStringToInt (const QString &baudRateString)
{
    QRegExp re ("(\\d+)");
    if (baudRateString.contains (re))
    {
        QString s = re.cap (1);
        bool ok = false;
        int result = s.toInt (&ok);
        if (ok)
            return result;
    }

    return -1;
}

/**
 * Implementation of DataStream::streamParametersCurrent ().
 *
 * This method is thread safe.
 */
bool SerialDataStream::streamParametersCurrent ()
{
	QMutexLocker backEndLocker (_backEndMutex);
	if (!_port->isOpen ())
		return false;
	QString activePortName = _port->deviceName ();
	int     activeBaudRate = baudRateStringToInt (_port->baudRate ());
	backEndLocker.unlock ();

	QMutexLocker parameterLocker (_parameterMutex);
	QString configuredPortName = _portName;
	int     configuredBaudRate = _baudRate;
	parameterLocker.unlock ();

	return
		configuredPortName == activePortName &&
		configuredBaudRate == activeBaudRate;
}


// *****************
// ** Port events **
// *****************

/**
 * Called when data is received from the port
 *
 * This method is thread safe.
 */
void SerialDataStream::port_dataReceived ()
{
	// Lock the back-end mutex and read the available data. Unlock the mutex
	// before calling the base class.
	QMutexLocker backEndLocker (_backEndMutex);
	QByteArray data=_port->readAll ();

	backEndLocker.unlock ();
	streamDataReceived (data);
}

/**
 * This method is thread safe.
 */
void SerialDataStream::port_status (const QString &status, QDateTime dateTime)
{
	// This method does not access any properties or call any methods. It is
	// therefore inherently thread safe and does not need locking.

	Q_UNUSED (status);
	Q_UNUSED (dateTime);
	//qDebug () << "Port status" << status << dateTime;
}

/**
 * This method is thread safe.
 */
void SerialDataStream::portAdded (QString port)
{
	// Lock the parameters mutex and make a copy of the configured port name.
	// Unlock the mutex afterwards.
	QMutexLocker parameterLocker (_parameterMutex);
	QString configuredPortName=_portName;
	parameterLocker.unlock ();

	if (port==configuredPortName)
	{
		// Oh joy: the port we are configured to use just became available.
		// FIXME use QTimer, or get rid
		//DefaultQThread::msleep (1000);
		streamConnectionBecameAvailable ();
	}
}

/**
 * This method is thread safe.
 */
void SerialDataStream::portRemoved (QString port)
{
	// Lock the back-end mutex, find out if we have the removed port open, and
	// react by closing the connection if appropriate. Unlock the mutex before
	// calling the base class.
	QMutexLocker backEndLocker (_backEndMutex);

	if (_port->isOpen ())
	{
		if (_port->deviceName ().toLower () == port.toLower ())
		{
			// Our port is no longer available
			_port->close ();

			backEndLocker.unlock ();
			streamError (tr ("The port is no longer available"));
		}
	}
}