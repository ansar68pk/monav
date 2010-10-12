/*
Copyright 2010  Christian Vetter veaac.fdirct@gmail.com

This file is part of MoNav.

MoNav is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

MoNav is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with MoNav.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "routinglogic.h"
#include "descriptiongenerator.h"
#include "mapdata.h"
#include "utils/qthelpers.h"

#include <QtDebug>

struct RoutingLogic::PrivateImplementation {
	GPSInfo gpsInfo;
	UnsignedCoordinate source;
	QVector< UnsignedCoordinate > waypoints;
	QVector< IRouter::Node > pathNodes;
	QVector< IRouter::Edge > pathEdges;
	double distance;
	double travelTime;
	DescriptionGenerator descriptionGenerator;
	QStringList labels;
	QStringList icons;
	bool linked;
#ifndef NOQTMOBILE
	// the current GPS source
	QGeoPositionInfoSource* gpsSource;
#endif
};

RoutingLogic::RoutingLogic() :
		d( new PrivateImplementation )
{
	d->linked = false;
	d->distance = -1;
	d->travelTime = -1;

	connect( MapData::instance(), SIGNAL(dataLoaded()), this, SLOT(dataLoaded()) );

#ifndef NOQTMOBILE
	d->gpsSource = QGeoPositionInfoSource::createDefaultSource( this );
	if ( d->gpsSource == NULL ) {
		qDebug() << "No GPS Sensor found! GPS Updates are not available";
	} else {
		d->gpsSource->startUpdates();
		connect( d->gpsSource, SIGNAL(positionUpdated(QGeoPositionInfo)), this, SLOT(positionUpdated(QGeoPositionInfo)) );
	}
#endif
}

RoutingLogic::~RoutingLogic()
{
#ifndef NOQTMOBILE
	if ( d->gpsSource != NULL )
		delete d->gpsSource;
#endif
	delete d;
}

RoutingLogic* RoutingLogic::instance()
{
	static RoutingLogic routingLogic;
	return &routingLogic;
}

#ifndef NOQTMOBILE
void RoutingLogic::positionUpdated( const QGeoPositionInfo& update )
{
	GPSCoordinate gps;
	gps.latitude = update.coordinate().latitude();
	gps.longitude = update.coordinate().longitude();
	d->gpsInfo.position = UnsignedCoordinate( gps );
	if ( update.hasAttribute( QGeoPositionInfo::Direction ) )
		d->gpsInfo.heading = update.attribute( QGeoPositionInfo::Direction );

	if ( d->linked )
		setSource( d->gpsInfo.position );

	emit gpsInfoChanged();
}
#endif

QVector< UnsignedCoordinate > RoutingLogic::waypoints() const
{
	return d->waypoints;
}

UnsignedCoordinate RoutingLogic::source() const
{
	return d->source;
}

UnsignedCoordinate RoutingLogic::target() const
{
	if ( d->waypoints.empty() )
		return UnsignedCoordinate();
	return d->waypoints.last();
}

bool RoutingLogic::gpsLink() const
{
	return d->linked;
}

const RoutingLogic::GPSInfo& RoutingLogic::gpsInfo() const
{
	return d->gpsInfo;
}

QVector< IRouter::Node > RoutingLogic::route() const
{
	return d->pathNodes;
}

void RoutingLogic::clear()
{
	d->waypoints.clear();
	computeRoute();
}

void RoutingLogic::instructions( QStringList* labels, QStringList* icons, int maxSeconds )
{
	d->descriptionGenerator.reset();
	d->descriptionGenerator.descriptions( &d->icons, &d->labels, d->pathNodes, d->pathEdges, maxSeconds );
	*labels = d->labels;
	*icons = d->icons;
}

void RoutingLogic::setWaypoint( int id, UnsignedCoordinate coordinate )
{
	if ( d->waypoints.size() <= id )
		d->waypoints.resize( id + 1 );
	d->waypoints[id] = coordinate;

	while ( !d->waypoints.empty() && !d->waypoints.back().IsValid() )
		d->waypoints.pop_back();

	computeRoute();

	emit waypointsChanged();
}

void RoutingLogic::setSource( UnsignedCoordinate coordinate )
{
	setGPSLink( false );
	d->source = coordinate;
	computeRoute();
	emit sourceChanged();
}

void RoutingLogic::setTarget( UnsignedCoordinate target )
{
	int index = d->waypoints.empty() ? 0 : d->waypoints.size() - 1;
	setWaypoint( index, target );
}

void RoutingLogic::setGPSLink( bool linked )
{
	if ( linked == d->linked )
		return;
	d->linked = true;
	if ( d->gpsInfo.position.IsValid() )
		setSource( d->gpsInfo.position );
	emit gpsLinkChanged( d->linked );
}

void RoutingLogic::computeRoute()
{
	IGPSLookup* gpsLookup = MapData::instance()->gpsLookup();
	if ( gpsLookup == NULL )
		return;
	IRouter* router = MapData::instance()->router();
	if ( router == NULL )
		return;

	if ( !d->source.IsValid() ) {
		clearRoute();
		return;
	}

	QVector< UnsignedCoordinate > waypoints;

	waypoints.push_back( d->source );
	for ( int i = 0; i < d->waypoints.size(); i++ ) {
		if ( d->waypoints[i].IsValid() )
			waypoints.push_back( d->waypoints[i] );
	}

	if ( waypoints.size() < 2 ) {
		clearRoute();
		return;
	}

	QVector< IGPSLookup::Result > gps;

	for ( int i = 0; i < waypoints.size(); i++ ) {
		Timer time;
		IGPSLookup::Result result;
		bool found = gpsLookup->GetNearestEdge( &result, waypoints[i], 1000 );
		qDebug() << "GPS Lookup:" << time.elapsed() << "ms";

		if ( !found ) {
			clearRoute();
			return;
		}

		gps.push_back( result );
	}

	d->pathNodes.clear();
	d->pathEdges.clear();

	for ( int i = 1; i < waypoints.size(); i++ ) {
		QVector< IRouter::Node > nodes;
		QVector< IRouter::Edge > edges;
		double travelTime;

		Timer time;
		bool found = router->GetRoute( &travelTime, &nodes, &edges, gps[i - 1], gps[i] );
		qDebug() << "Routing:" << time.elapsed() << "ms";

		if ( found ) {
			if ( i == 1 ) {
				d->pathNodes = nodes;
				d->pathEdges = edges;
			} else {
				for ( int j = 1; j < nodes.size(); j++ )
					d->pathNodes.push_back( nodes[j] );
				for ( int j = 1; j < edges.size(); j++ )
					d->pathEdges.push_back( edges[j] );
			}
			d->travelTime += travelTime;
		} else {
			d->travelTime = -1;
			break;
		}
	}

	d->distance = waypoints.first().ToGPSCoordinate().ApproximateDistance( waypoints.last().ToGPSCoordinate() );

	emit routeChanged();
	emit instructionsChanged();
	emit distanceChanged( d->distance );
	emit travelTimeChanged( d->travelTime );
}

void RoutingLogic::clearRoute()
{
	d->distance = -1;
	d->travelTime = -1;
	d->pathEdges.clear();
	d->pathNodes.clear();
	d->icons.clear();
	d->labels.clear();
	emit routeChanged();
	emit instructionsChanged();
	emit distanceChanged( d->distance );
	emit travelTimeChanged( d->travelTime );
}

void RoutingLogic::dataLoaded()
{
	computeRoute();
}
