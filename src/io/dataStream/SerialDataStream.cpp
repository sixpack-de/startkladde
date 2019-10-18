#include "src/io/dataStream/SerialDataStream.h"

#include <QDebug>
#include <QMutex>
#include <QMutexLocker>

#include "src/text.h"
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


	// Create the port and connect the required signals. _port will be deleted
	// automatically by its parent.
    _port = new QSerialPort(this);
	connect (_port, SIGNAL (readyRead         ()),
	         this , SLOT   (port_dataReceived ()));
    connect (_port, SIGNAL (errorOccurred (QSerialPort::SerialPortError)),
             this , SLOT   (port_errorOccurred (QSerialPort::SerialPortError)));

    _timer = new QTimer(this);
    connect(_timer, &QTimer::timeout, this, &SerialDataStream::timer_timeout);

    _timer->start(1000);
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
    QSerialPortInfo portInfo;
    foreach(QSerialPortInfo p, QSerialPortInfo::availablePorts())
    {
        if (p.portName().toLower() == portName.toLower())
            portInfo = p;
    }

    if (portInfo.isNull())
	{
		backEndLocker.unlock ();
		streamError (tr ("The port %1 does not exist").arg (portName));
		return;
	}

	// Open the port and check success.
    _port->setPort (portInfo);
	_port->open (QIODevice::ReadOnly);
    if (_port->isOpen ())
	{
		// Configure the port. The port is currently configured as the last user of
		// the port left it.
		_port->setBaudRate    (baudRate);
        _port->setDataBits    (QSerialPort::DataBits::Data8);
        _port->setParity      (QSerialPort::Parity::NoParity);
        _port->setStopBits    (QSerialPort::StopBits::OneStop);
        _port->setFlowControl (QSerialPort::FlowControl::NoFlowControl);
        _port->setDataTerminalReady (true);
        _port->setRequestToSend     (true);

		backEndLocker.unlock ();
		streamOpened ();
	}
	else
	{
		QString errorMessage=_port->errorString ();
		backEndLocker.unlock ();
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
    QString activePortName = _port->portName ();
    int     activeBaudRate = _port->baudRate ();
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
void SerialDataStream::port_errorOccurred(QSerialPort::SerialPortError error)
{
    Q_UNUSED (error);

    // The open port has raised an error, close it therefore and report
    // that the port is no longer available.
    QMutexLocker backEndLocker (_backEndMutex);
    _port->close();
    backEndLocker.unlock();

    streamError (tr ("The port is no longer available"));
}

/**
 * This method is thread safe.
 */
void SerialDataStream::timer_timeout ()
{
    // Fetch activity state and configured port name
    // using the respective mutexes
    QMutexLocker backEndLocker (_backEndMutex);
    bool open = _port->isOpen();
    backEndLocker.unlock();
    QMutexLocker parameterLocker (_parameterMutex);
    QString configuredPortName=_portName;
    parameterLocker.unlock ();

    // If a port matching the configured port name is present
    // then report the good news to the superclass
    // (of course do this only if the port is not open already)
    if (!open)
    {
        foreach(QSerialPortInfo p, QSerialPortInfo::availablePorts())
        {
            if (p.portName().toLower() == configuredPortName) {
                streamConnectionBecameAvailable();
                return;
            }
        }
    }

}
