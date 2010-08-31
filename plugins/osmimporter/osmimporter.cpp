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

#include "osmimporter.h"
#include "bz2input.h"
#include <algorithm>
#include <QtDebug>
#include "utils/qthelpers.h"

OSMImporter::OSMImporter()
{
	Q_INIT_RESOURCE(speedprofiles);
	m_settingsDialog = NULL;
	m_kmhStrings.push_back( "%.lf" );
	m_kmhStrings.push_back( "%.lf kmh" );
	m_kmhStrings.push_back( "%.lf km/h" );
	m_kmhStrings.push_back( "%.lfkmh" );
	m_kmhStrings.push_back( "%.lfkm/h" );
	m_kmhStrings.push_back( "%lf" );
	m_kmhStrings.push_back( "%lf kmh" );
	m_kmhStrings.push_back( "%lf km/h" );
	m_kmhStrings.push_back( "%lfkmh" );
	m_kmhStrings.push_back( "%lfkm/h" );

	m_mphStrings.push_back( "%.lf mph" );
	m_mphStrings.push_back( "%.lfmph" );
	m_mphStrings.push_back( "%lf mph" );
	m_mphStrings.push_back( "%lfmph" );
}

OSMImporter::~OSMImporter()
{
	Q_CLEANUP_RESOURCE(speedprofiles);
	if ( m_settingsDialog != NULL )
		delete m_settingsDialog;
}

QString OSMImporter::GetName()
{
	return "OpenStreetMap Importer";
}

void OSMImporter::SetOutputDirectory( const QString& dir )
{
	m_outputDirectory = dir;
}

void OSMImporter::ShowSettings()
{
	if ( m_settingsDialog == NULL )
		m_settingsDialog = new OISettingsDialog;
	m_settingsDialog->exec();
}

bool OSMImporter::Preprocess()
{
	if ( m_settingsDialog == NULL )
		m_settingsDialog = new OISettingsDialog();
	if ( !m_settingsDialog->getSettings( &m_settings ) )
		return false;
	if ( m_settings.speedProfile.names.size() == 0 ) {
		qCritical( "no speed profile specified" );
		return false;
	}

	m_usedNodes.clear();
	m_outlineNodes.clear();
	m_signalNodes.clear();
	QString filename = fileInDirectory( m_outputDirectory, "OSM Importer" );

	m_statistics.numberOfNodes = 0;
	m_statistics.numberOfEdges = 0;
	m_statistics.numberOfWays = 0;
	m_statistics.numberOfPlaces = 0;
	m_statistics.numberOfOutlines = 0;
	m_statistics.numberOfMaxspeed = 0;
	m_statistics.numberOfZeroSpeed = 0;
	m_statistics.numberOfDefaultCitySpeed = 0;
	m_statistics.numberOfCityEdges = 0;

	Timer time;

	if ( !readXML( m_settings.input, filename ) )
		return false;
	qDebug() << "OSM Importer: finished import pass 1:" << time.restart() << "ms";

	if ( m_usedNodes.size() == 0 ) {
		qCritical( "OSM Importer: no routing nodes found in the data set" );
		return false;
	}

	std::sort( m_usedNodes.begin(), m_usedNodes.end() );
	m_usedNodes.resize( std::unique( m_usedNodes.begin(), m_usedNodes.end() ) - m_usedNodes.begin() );
	std::sort( m_outlineNodes.begin(), m_outlineNodes.end() );
	m_outlineNodes.resize( std::unique( m_outlineNodes.begin(), m_outlineNodes.end() ) - m_outlineNodes.begin() );
	std::sort( m_signalNodes.begin(), m_signalNodes.end() );

	if ( !preprocessData( filename ) )
		return false;
	qDebug() << "OSM Importer: finished import pass 2:" << time.restart() << "ms";

	qDebug() << "OSM Importer: Nodes:" << m_statistics.numberOfNodes;
	qDebug() << "OSM Importer: Ways:" << m_statistics.numberOfWays;
	qDebug() << "OSM Importer: Places:" << m_statistics.numberOfPlaces;
	qDebug() << "OSM Importer: Places Outlines:" << m_statistics.numberOfOutlines;
	qDebug() << "OSM Importer: Places Outline Nodes:" << ( int ) m_outlineNodes.size();
	qDebug() << "OSM Importer: Edges:" << m_statistics.numberOfEdges;
	qDebug() << "OSM Importer: Routing Nodes:" << ( int ) m_usedNodes.size();
	qDebug() << "OSM Importer: Traffic Signal Nodes:" << ( int ) m_signalNodes.size();
	qDebug() << "OSM Importer: #Maxspeed Specified:" << m_statistics.numberOfMaxspeed;
	qDebug() << "OSM Importer: Number Of Zero Speed Ways:" << m_statistics.numberOfZeroSpeed;
	qDebug() << "OSM Importer: Number Of Edges with Default City Speed:" << m_statistics.numberOfDefaultCitySpeed;

	m_usedNodes.clear();
	m_outlineNodes.clear();
	m_signalNodes.clear();
	return true;
}

bool OSMImporter::readXML( const QString& inputFilename, const QString& filename ) {
	FileStream edgesData( filename + "_edges" );
	FileStream placesData( filename + "_places" );
	FileStream boundingBoxData( filename + "_bounding_box" );
	FileStream allNodesData( filename + "_all_nodes" );
	FileStream cityOutlineData( filename + "_city_outlines" );

	if ( !edgesData.open( QIODevice::WriteOnly ) )
		return false;
	if ( !placesData.open( QIODevice::WriteOnly ) )
		return false;
	if ( !boundingBoxData.open( QIODevice::WriteOnly ) )
		return false;
	if ( !allNodesData.open( QIODevice::WriteOnly ) )
		return false;
	if ( !cityOutlineData.open( QIODevice::WriteOnly ) )
		return false;

	xmlTextReaderPtr inputReader;
	if ( inputFilename.endsWith( ".bz2" ) )
		inputReader = getBz2Reader( inputFilename.toLocal8Bit().constData() );
	else
		inputReader = xmlNewTextReaderFilename( inputFilename.toLocal8Bit().constData() );

	if ( inputReader == NULL ) {
		qCritical() << "failed to open XML reader";
		return false;
	}

	try {
		while ( xmlTextReaderRead( inputReader ) == 1 ) {
			const int type = xmlTextReaderNodeType( inputReader );

			//1 is Element
			if ( type != 1 )
				continue;

			xmlChar* currentName = xmlTextReaderName( inputReader );
			if ( currentName == NULL )
				continue;

			if ( xmlStrEqual( currentName, ( const xmlChar* ) "node" ) == 1 ) {
				m_statistics.numberOfNodes++;
				Node node = readXMLNode( inputReader );

				if ( node.trafficSignal )
					m_signalNodes.push_back( node.id );

				allNodesData << quint32( node.id ) << node.latitude << node.longitude;

				if ( node.type != Place::None && node.name != NULL ) {
					placesData << node.latitude << node.longitude << quint32( node.type ) << quint32( node.population ) << QString::fromUtf8( ( const char* ) node.name );
					m_statistics.numberOfPlaces++;
				}
				if ( node.name != NULL )
					xmlFree( node.name );
			}


			else if ( xmlStrEqual( currentName, ( const xmlChar* ) "way" ) == 1 ) {
				m_statistics.numberOfWays++;
				Way way = readXMLWay( inputReader );

				if ( way.usefull && way.access && way.path.size() > 0 ) {
					for ( unsigned i = 0; i < way.path.size(); ++i ) {
						m_usedNodes.push_back( way.path[i] );
					}

					if ( way.name != NULL )
						edgesData << QString::fromUtf8( ( const char* ) way.name );
					else
						edgesData << QString( "" );

					if ( m_settings.ignoreOneway )
						way.direction = Way::Bidirectional;
					if ( m_settings.ignoreMaxspeed )
						way.maximumSpeed = -1;

					edgesData << qint32( way.type );
					edgesData << way.maximumSpeed;
					edgesData << qint32(( way.direction == Way::Oneway || way.direction == Way::Opposite ) ? 0 : 1 );
					edgesData << qint32( way.path.size() );

					if ( way.direction == Way::Opposite )
						std::reverse( way.path.begin(), way.path.end() );

					for ( int i = 0; i < ( int ) way.path.size(); ++i )
						edgesData << quint32( way.path[i] );

					m_statistics.numberOfEdges += ( int ) way.path.size() - 1;
				}

				if ( way.placeType != Place::None && way.path.size() > 1 && way.path[0] == way.path[way.path.size() - 1] && way.placeName != NULL ) {
					cityOutlineData << quint32( way.placeType ) << quint32( way.path.size() - 1 );
					if ( way.placeName != NULL )
						cityOutlineData << QString::fromUtf8( ( const char* ) way.placeName );
					else
						cityOutlineData << QString( "" );
					for ( unsigned i = 1; i < way.path.size(); ++i ) {
						m_outlineNodes.push_back( way.path[i] );
						cityOutlineData << quint32( way.path[i] );
					}
					m_statistics.numberOfOutlines++;
				}

				if ( way.name != NULL )
					xmlFree( way.name );
				if ( way.placeName != NULL )
					xmlFree( way.placeName );
			}


			else if ( xmlStrEqual( currentName, ( const xmlChar* ) "bound" ) == 1 ) {
				xmlChar* box = xmlTextReaderGetAttribute( inputReader, ( const xmlChar* ) "box" );
				if ( box != NULL ) {
					QString boxString = QString::fromUtf8( ( const char* ) box );
					QStringList coordinateList = boxString.split( ',' );
					if ( coordinateList.size() != 4 )
					{
						qCritical( "OSM Importer: bounding box not valid!" );
						return false;
					}
					double temp;
					temp = coordinateList[0].toDouble();
					boundingBoxData << temp;
					temp = coordinateList[1].toDouble();
					boundingBoxData << temp;
					temp = coordinateList[2].toDouble();
					boundingBoxData << temp;
					temp = coordinateList[3].toDouble();
					boundingBoxData << temp;
					xmlFree( box );
				}
			}

			xmlFree( currentName );
		}

	} catch ( const std::exception& e ) {
		qCritical( "OSM Importer: caught execption: %s", e.what() );
		return false;
	}
	return true;
}

bool OSMImporter::preprocessData( const QString& filename ) {
	std::vector< GPSCoordinate > nodeCoordinates( m_usedNodes.size(), GPSCoordinate( -1, -1 ) );
	std::vector< GPSCoordinate > outlineCoordinates( m_outlineNodes.size(), GPSCoordinate( -1, -1 ) );

	FileStream allNodesData( filename + "_all_nodes" );
	FileStream edgesData( filename + "_edges" );
	FileStream cityOutlinesData( filename + "_city_outlines" );
	FileStream placesData( filename + "_places" );

	if ( !allNodesData.open( QIODevice::ReadOnly ) )
		return false;
	if ( !edgesData.open( QIODevice::ReadOnly ) )
		return false;
	if ( !cityOutlinesData.open( QIODevice::ReadOnly ) )
		return false;
	if ( !placesData.open( QIODevice::ReadOnly ) )
		return false;

	FileStream nodeCoordinatesData( filename + "_node_coordinates" );
	FileStream mappedEdgesData( filename + "_mapped_edges" );
	FileStream locationData( filename + "_location" );

	if ( !nodeCoordinatesData.open( QIODevice::WriteOnly ) )
		return false;
	if ( !mappedEdgesData.open( QIODevice::WriteOnly ) )
		return false;
	if ( !locationData.open( QIODevice::WriteOnly ) )
		return false;

	Timer time;

	while ( true ) {
		quint32 node;
		GPSCoordinate gps;
		allNodesData >> node >> gps.latitude >> gps.longitude;
		if ( allNodesData.status() == QDataStream::ReadPastEnd )
			break;
		std::vector< NodeID >::const_iterator element = std::lower_bound( m_usedNodes.begin(), m_usedNodes.end(), node );
		if ( element != m_usedNodes.end() && *element == node ) {
			nodeCoordinates[element - m_usedNodes.begin()] = gps;
		}
		element = std::lower_bound( m_outlineNodes.begin(), m_outlineNodes.end(), node );
		if ( element != m_outlineNodes.end() && *element == node ) {
			outlineCoordinates[element - m_outlineNodes.begin()] = gps;
		}
	}

	qDebug() << "OSM Importer: filtered node coordinates:" << time.restart() << "ms";

	for ( std::vector< NodeID >::const_iterator i = m_usedNodes.begin(); i != m_usedNodes.end(); ++i ) {
		NodeID node = i - m_usedNodes.begin();
		nodeCoordinatesData << nodeCoordinates[node].latitude << nodeCoordinates[node].longitude;
		if ( nodeCoordinates[node].latitude == -1 && nodeCoordinates[node].longitude == -1 )
			qDebug( "OSM Importer: inconsistent OSM data: missing way node coordinate %d" , ( int ) node );
	}

	qDebug() << "OSM Importer: wrote routing node coordinates:" << time.restart() << "ms";

	std::vector< Outline > cityOutlines;
	while ( true ) {
		Outline outline;
		quint32 type, numberOfPathNodes;
		cityOutlinesData >> type >> numberOfPathNodes >> outline.name;
		if ( cityOutlinesData.status() == QDataStream::ReadPastEnd )
			break;

		bool valid = true;
		for ( int i = 0; i < ( int ) numberOfPathNodes; ++i ) {
			quint32 node;
			cityOutlinesData >> node;
			NodeID mappedNode = std::lower_bound( m_outlineNodes.begin(), m_outlineNodes.end(), node ) - m_outlineNodes.begin();
			UnsignedCoordinate coordinate( outlineCoordinates[mappedNode] );
			if ( outlineCoordinates[mappedNode].latitude == -1 && outlineCoordinates[mappedNode].longitude == -1 ) {
				qDebug( "OSM Importer: inconsistent OSM data: missing outline node coordinate %d", ( int ) mappedNode );
				valid = false;
			}
			DoublePoint point( coordinate.x, coordinate.y );
			outline.way.push_back( point );
		}
		if ( valid )
			cityOutlines.push_back( outline );
	}
	outlineCoordinates.clear();
	std::sort( cityOutlines.begin(), cityOutlines.end() );

	qDebug() << "OSM Importer: read city outlines:" << time.restart() << "s";

	std::vector< Location > places;
	while ( true ) {
		Location place;
		quint32 type;
		quint32 population;
		placesData >> place.coordinate.latitude >> place.coordinate.longitude >> type >> population >> place.name;

		if ( placesData.status() == QDataStream::ReadPastEnd )
			break;

		place.type = ( Place::Type ) type;
		places.push_back( place );
	}

	qDebug() << "OSM Importer: read places:" << time.restart() << "ms";

	typedef GPSTree::InputPoint InputPoint;
	std::vector< InputPoint > kdPoints;
	kdPoints.reserve( m_usedNodes.size() );
	std::vector< NodeLocation > nodeLocation( m_usedNodes.size() );
	for ( std::vector< GPSCoordinate >::const_iterator node = nodeCoordinates.begin(), endNode = nodeCoordinates.end(); node != endNode; ++node ) {
		InputPoint point;
		point.data = node - nodeCoordinates.begin();
		point.coordinates[0] = node->latitude;
		point.coordinates[1] = node->longitude;
		kdPoints.push_back( point );
		nodeLocation[point.data].isInPlace = false;
		nodeLocation[point.data].distance = std::numeric_limits< double >::max();
	}
	GPSTree* kdTree = new GPSTree( kdPoints );
	kdPoints.clear();

	qDebug() << "OSM Importer: build kd-tree:" << time.restart() << "ms";

	for ( std::vector< Location >::const_iterator place = places.begin(), endPlace = places.end(); place != endPlace; ++place ) {
		InputPoint point;
		point.coordinates[0] = place->coordinate.latitude;
		point.coordinates[1] = place->coordinate.longitude;
		std::vector< InputPoint > result;

		const Outline* placeOutline = NULL;
		double radius = 0;
		Outline searchOutline;
		searchOutline.name = place->name;
		for ( std::vector< Outline >::const_iterator outline = std::lower_bound( cityOutlines.begin(), cityOutlines.end(), searchOutline ), outlineEnd = std::upper_bound( cityOutlines.begin(), cityOutlines.end(), searchOutline ); outline != outlineEnd; ++outline ) {
			UnsignedCoordinate cityCoordinate = UnsignedCoordinate( place->coordinate );
			DoublePoint cityPoint( cityCoordinate.x, cityCoordinate.y );
			if ( pointInPolygon( outline->way.size(), &outline->way[0], cityPoint ) ) {
				placeOutline = &( *outline );
				for ( std::vector< DoublePoint >::const_iterator way = outline->way.begin(), wayEnd = outline->way.end(); way != wayEnd; ++way ) {
					UnsignedCoordinate coordinate;
					coordinate.x = way->x;
					coordinate.y = way->y;
					double distance = coordinate.ToGPSCoordinate().ApproximateDistance( place->coordinate );
					radius = std::max( radius, distance );
				}
				break;
			}
		}

		if ( placeOutline != NULL ) {
			kdTree->NearNeighbors( &result, point, radius );
			for ( std::vector< InputPoint >::const_iterator i = result.begin(), e = result.end(); i != e; ++i ) {
				GPSCoordinate gps;
				gps.latitude = i->coordinates[0];
				gps.longitude = i->coordinates[1];
				UnsignedCoordinate coordinate = UnsignedCoordinate( gps );
				DoublePoint nodePoint;
				nodePoint.x = coordinate.x;
				nodePoint.y = coordinate.y;
				if ( !pointInPolygon( placeOutline->way.size(), &placeOutline->way[0], nodePoint ) )
					continue;
				nodeLocation[i->data].isInPlace = true;
				nodeLocation[i->data].place = place - places.begin();
				nodeLocation[i->data].distance = 0;
			}
		} else {
			switch ( place->type ) {
			case Place::None:
				continue;
			case Place::Suburb:
				continue;
			case Place::Hamlet:
				kdTree->NearNeighbors( &result, point, 300 );
				break;
			case Place::Village:
				kdTree->NearNeighbors( &result, point, 1000 );
				break;
			case Place::Town:
				kdTree->NearNeighbors( &result, point, 5000 );
				break;
			case Place::City:
				kdTree->NearNeighbors( &result, point, 10000 );
				break;
			}

			for ( std::vector< InputPoint >::const_iterator i = result.begin(), e = result.end(); i != e; ++i ) {
				GPSCoordinate gps;
				gps.latitude = i->coordinates[0];
				gps.longitude = i->coordinates[1];
				double distance =  gps.ApproximateDistance( place->coordinate );
				if ( distance >= nodeLocation[i->data].distance )
					continue;
				nodeLocation[i->data].isInPlace = true;
				nodeLocation[i->data].place = place - places.begin();
				nodeLocation[i->data].distance = distance;
			}
		}
	}

	delete kdTree;
	places.clear();
	cityOutlines.clear();

	qDebug() << "OSM Importer: assigned 'in-city' flags:" << time.restart() << "ms";

	for ( std::vector< NodeLocation >::const_iterator i = nodeLocation.begin(), e = nodeLocation.end(); i != e; ++i ) {
		locationData << quint32( i->isInPlace ? 1 : 0 ) << quint32( i->place );
	}

	qDebug() << "OSM Importer: wrote 'in-city' flags" << time.restart() << "ms";

	while ( true ) {
		QString name;
		double speed;
		qint32 bidirectional, numberOfPathNodes, type;
		std::vector< NodeID > way;
		edgesData >> name >> type >> speed >> bidirectional >> numberOfPathNodes;
		if ( edgesData.status() == QDataStream::ReadPastEnd )
			break;

		bool valid = true;
		if ( speed == 0 )
			valid = false;
		for ( int i = 0; i < ( int ) numberOfPathNodes; ++i ) {
			quint32 node;
			edgesData >> node;
			if ( !valid )
				continue;

			NodeID mappedNode = std::lower_bound( m_usedNodes.begin(), m_usedNodes.end(), node ) - m_usedNodes.begin();
			way.push_back( mappedNode );
			if ( nodeCoordinates[mappedNode].latitude == -1 && nodeCoordinates[mappedNode].longitude == -1 ) {
				qDebug( "OSM Importer: inconsistent OSM data: skipping way with missing node coordinate %d", ( int ) mappedNode );
				valid = false;
			}
		}
		if ( !valid )
			continue;

		mappedEdgesData << name << bidirectional << numberOfPathNodes;

		for ( int i = 0; i < ( int ) numberOfPathNodes; i++ ) {
			mappedEdgesData << way[i];
		}

		if ( speed == 0 || ( speed < 0 && type < 0 ) ) {
			m_statistics.numberOfZeroSpeed++;
			continue;
		}
		if ( type < 0 )
			type = m_settings.speedProfile.names.size();

		for ( int i = 1; i < ( int ) numberOfPathNodes; ++i ) {
			GPSCoordinate fromCoordinate = nodeCoordinates[way[i - 1]];
			GPSCoordinate toCoordinate = nodeCoordinates[way[i]];
			double distance = fromCoordinate.Distance( toCoordinate );
			double tempSpeed = speed;
			if ( tempSpeed < 0 ) {
				assert( type < ( int ) m_settings.speedProfile.names.size() );
				if ( m_settings.defaultCitySpeed && ( nodeLocation[way[i - 1]].isInPlace || nodeLocation[way[i]].isInPlace ) ) {
					m_statistics.numberOfDefaultCitySpeed++;
					tempSpeed = m_settings.speedProfile.speedInCity[type];
				}
				else {
					tempSpeed = m_settings.speedProfile.speed[type];
				}
			}

			if ( type < ( int ) m_settings.speedProfile.names.size()  ) {
				if ( nodeLocation[way[i - 1]].isInPlace || nodeLocation[way[i]].isInPlace ) {
					m_statistics.numberOfCityEdges++;
				}
				tempSpeed *= m_settings.speedProfile.averagePercentage[type] / 100.0;
			}
			double seconds = distance * 36 / tempSpeed;

			if ( seconds < 0 )
				qCritical() << "OSM Importer: distance less than Zero:" << seconds;
			if ( seconds > 24 * 60 * 60 ) {
				qDebug() << "OSM Importer: found very large edge:" << distance * 36 / tempSpeed << "seconds";
				qDebug() << "OSM Importer: found very large edge:" << way[i-1] << "->" << way[i];
				qDebug() << "OSM Importer: found very large edge: (" << fromCoordinate.latitude << "," << fromCoordinate.longitude << ") -> (" << toCoordinate.latitude << "," << toCoordinate.longitude << ")";
				qDebug() << "OSM Importer: found very large edge:" << tempSpeed << "km/h";
			}

			std::vector< NodeID >::const_iterator sourceNode = std::lower_bound( m_signalNodes.begin(), m_signalNodes.end(), way[i - 1] );
			std::vector< NodeID >::const_iterator targetNode = std::lower_bound( m_signalNodes.begin(), m_signalNodes.end(), way[i] );
			if ( sourceNode != m_signalNodes.end() && *sourceNode == way[i - 1] )
				seconds += m_settings.trafficLightPenalty / 2.0;
			if ( targetNode != m_signalNodes.end() && *targetNode == way[i] )
				seconds += m_settings.trafficLightPenalty / 2.0;

			mappedEdgesData << seconds;
		}

	}

	qDebug() << "OSM Importer: remapped edges" << time.restart() << "ms";

	return true;
}

OSMImporter::Way OSMImporter::readXMLWay( xmlTextReaderPtr& inputReader ) {
	Way way;
	way.direction = Way::NotSure;
	way.maximumSpeed = -1;
	way.type = -1;
	way.name = NULL;
	way.placeType = Place::None;
	way.placeName = NULL;
	way.usefull = false;
	way.access = true;
	way.accessPriority = m_settings.accessList.size();

	if ( xmlTextReaderIsEmptyElement( inputReader ) != 1 ) {
		const int depth = xmlTextReaderDepth( inputReader );
		while ( xmlTextReaderRead( inputReader ) == 1 ) {
			const int childType = xmlTextReaderNodeType( inputReader );
			if ( childType != 1 && childType != 15 )
				continue;
			const int childDepth = xmlTextReaderDepth( inputReader );
			xmlChar* childName = xmlTextReaderName( inputReader );
			if ( childName == NULL )
				continue;

			if ( depth == childDepth && childType == 15 && xmlStrEqual( childName, ( const xmlChar* ) "way" ) == 1 ) {
				xmlFree( childName );
				break;
			}
			if ( childType != 1 ) {
				xmlFree( childName );
				continue;
			}

			if ( xmlStrEqual( childName, ( const xmlChar* ) "tag" ) == 1 ) {
				xmlChar* k = xmlTextReaderGetAttribute( inputReader, ( const xmlChar* ) "k" );
				xmlChar* value = xmlTextReaderGetAttribute( inputReader, ( const xmlChar* ) "v" );
				if ( k != NULL && value != NULL ) {
					if ( xmlStrEqual( k, ( const xmlChar* ) "oneway" ) == 1 ) {
						if ( xmlStrEqual( value, ( const xmlChar* ) "no" ) == 1 || xmlStrEqual( value, ( const xmlChar* ) "false" ) == 1 || xmlStrEqual( value, ( const xmlChar* ) "0" ) == 1 )
							way.direction = Way::Bidirectional;
						else if ( xmlStrEqual( value, ( const xmlChar* ) "yes" ) == 1 || xmlStrEqual( value, ( const xmlChar* ) "true" ) == 1 || xmlStrEqual( value, ( const xmlChar* ) "1" ) == 1 )
							way.direction = Way::Oneway;
						else if ( xmlStrEqual( value, ( const xmlChar* ) "-1" ) == 1 )
							way.direction = Way::Opposite;
					} else if ( xmlStrEqual( k, ( const xmlChar* ) "junction" ) == 1 ) {
						if ( xmlStrEqual( value, ( const xmlChar* ) "roundabout" ) == 1 ) {
							if ( way.direction == Way::NotSure ) {
								way.direction = Way::Oneway;
							}
							//if ( way.maximumSpeed == -1 )
							//	way.maximumSpeed = 10;
						}
					} else if ( xmlStrEqual( k, ( const xmlChar* ) "highway" ) == 1 ) {
						if ( xmlStrEqual( value, ( const xmlChar* ) "motorway" ) == 1 ) {
							if ( way.direction == Way::NotSure ) {
								way.direction = Way::Oneway;
							}
						} else if ( xmlStrEqual( value, ( const xmlChar* ) "motorway_link" ) == 1 ) {
							if ( way.direction == Way::NotSure ) {
								way.direction = Way::Oneway;
							}
						}
						{
							QString name = QString::fromUtf8( ( const char* ) value );
							for ( int i = 0; i < m_settings.speedProfile.names.size(); i++ ) {
								if ( name == m_settings.speedProfile.names[i] ) {
									way.type = i;
									way.usefull = true;
									break;
								}
							}
						}
					} else if ( xmlStrEqual( k, ( const xmlChar* ) "name" ) == 1 ) {
						way.name = xmlStrdup( value );
					} else if ( xmlStrEqual( k, ( const xmlChar* ) "place_name" ) ) {
						way.placeName = xmlStrdup( value );
					} else if ( xmlStrEqual( k, ( const xmlChar* ) "place" ) ) {
						if ( xmlStrEqual( value, ( const xmlChar* ) "city" ) == 1 )
							way.placeType = Place::City;
						else if ( xmlStrEqual( value, ( const xmlChar* ) "town" ) == 1 )
							way.placeType = Place::Town;
						else if ( xmlStrEqual( value, ( const xmlChar* ) "village" ) == 1 )
							way.placeType = Place::Village;
						else if ( xmlStrEqual( value, ( const xmlChar* ) "hamlet" ) == 1 )
							way.placeType = Place::Hamlet;
						else if ( xmlStrEqual( value, ( const xmlChar* ) "suburb" ) == 1 )
							way.placeType = Place::Suburb;
					} else if ( xmlStrEqual( k, ( const xmlChar* ) "maxspeed" ) == 1 ) {
						double maxspeed = atof(( const char* ) value );

						xmlChar buffer[100];
						for ( int i = 0; i < ( int ) m_kmhStrings.size(); i++ ) {
							xmlStrPrintf( buffer, 100, ( const xmlChar* ) m_kmhStrings[i], maxspeed );
							if ( xmlStrEqual( value, buffer ) == 1 ) {
								way.maximumSpeed = maxspeed;
								m_statistics.numberOfMaxspeed++;
								break;
							}
						}
						for ( int i = 0; i < ( int ) m_mphStrings.size(); i++ ) {
							xmlStrPrintf( buffer, 100, ( const xmlChar* ) m_mphStrings[i], maxspeed );
							if ( xmlStrEqual( value, buffer ) == 1 ) {
								way.maximumSpeed = maxspeed * 1.609344;
								m_statistics.numberOfMaxspeed++;
								break;
							}
						}
					} else {
						QString key = QString::fromUtf8( ( const char* ) k );
						int index = m_settings.accessList.indexOf( key );
						if ( index != -1 && index < way.accessPriority ) {
							if ( xmlStrEqual( value, ( const xmlChar* ) "private" ) == 1
									|| xmlStrEqual( value, ( const xmlChar* ) "no" ) == 1
									|| xmlStrEqual( value, ( const xmlChar* ) "agricultural" ) == 1
									|| xmlStrEqual( value, ( const xmlChar* ) "forestry" ) == 1
									|| xmlStrEqual( value, ( const xmlChar* ) "delivery" ) == 1
									) {
								way.access = false;
								way.accessPriority = index;
							}
							else if ( xmlStrEqual( value, ( const xmlChar* ) "yes" ) == 1
									|| xmlStrEqual( value, ( const xmlChar* ) "designated" ) == 1
									|| xmlStrEqual( value, ( const xmlChar* ) "official" ) == 1
									|| xmlStrEqual( value, ( const xmlChar* ) "permissive" ) == 1
									) {
								way.access = true;
								way.accessPriority = index;
							}
						}
					}

					if ( k != NULL )
						xmlFree( k );
					if ( value != NULL )
						xmlFree( value );
				}
			} else if ( xmlStrEqual( childName, ( const xmlChar* ) "nd" ) == 1 ) {
				xmlChar* ref = xmlTextReaderGetAttribute( inputReader, ( const xmlChar* ) "ref" );
				if ( ref != NULL ) {
					way.path.push_back( atoi(( const char* ) ref ) );
					xmlFree( ref );
				}
			}

			xmlFree( childName );
		}

	}
	return way;
}

OSMImporter::Node OSMImporter::readXMLNode( xmlTextReaderPtr& inputReader ) {
	Node node;
	node.name = NULL;
	node.type = Place::None;
	node.population = -1;
	node.trafficSignal = false;

	xmlChar* attribute = xmlTextReaderGetAttribute( inputReader, ( const xmlChar* ) "lat" );
	if ( attribute != NULL ) {
		node.latitude =  atof(( const char* ) attribute );
		xmlFree( attribute );
	}
	attribute = xmlTextReaderGetAttribute( inputReader, ( const xmlChar* ) "lon" );
	if ( attribute != NULL ) {
		node.longitude =  atof(( const char* ) attribute );
		xmlFree( attribute );
	}
	attribute = xmlTextReaderGetAttribute( inputReader, ( const xmlChar* ) "id" );
	if ( attribute != NULL ) {
		node.id =  atoi(( const char* ) attribute );
		xmlFree( attribute );
	}

	if ( xmlTextReaderIsEmptyElement( inputReader ) != 1 ) {
		const int depth = xmlTextReaderDepth( inputReader );
		while ( xmlTextReaderRead( inputReader ) == 1 ) {
			const int childType = xmlTextReaderNodeType( inputReader );
			// 1 = Element, 15 = EndElement
			if ( childType != 1 && childType != 15 )
				continue;
			const int childDepth = xmlTextReaderDepth( inputReader );
			xmlChar* childName = xmlTextReaderName( inputReader );
			if ( childName == NULL )
				continue;

			if ( depth == childDepth && childType == 15 && xmlStrEqual( childName, ( const xmlChar* ) "node" ) == 1 ) {
				xmlFree( childName );
				break;
			}
			if ( childType != 1 ) {
				xmlFree( childName );
				continue;
			}

			if ( xmlStrEqual( childName, ( const xmlChar* ) "tag" ) == 1 ) {
				xmlChar* k = xmlTextReaderGetAttribute( inputReader, ( const xmlChar* ) "k" );
				xmlChar* value = xmlTextReaderGetAttribute( inputReader, ( const xmlChar* ) "v" );
				if ( k != NULL && value != NULL ) {
					if ( xmlStrEqual( k, ( const xmlChar* ) "place" ) == 1 ) {
						if ( xmlStrEqual( value, ( const xmlChar* ) "city" ) == 1 )
							node.type = Place::City;
						else if ( xmlStrEqual( value, ( const xmlChar* ) "town" ) == 1 )
							node.type = Place::Town;
						else if ( xmlStrEqual( value, ( const xmlChar* ) "village" ) == 1 )
							node.type = Place::Village;
						else if ( xmlStrEqual( value, ( const xmlChar* ) "hamlet" ) == 1 )
							node.type = Place::Hamlet;
						else if ( xmlStrEqual( value, ( const xmlChar* ) "suburb" ) == 1 )
							node.type = Place::Suburb;
					} else if ( xmlStrEqual( k, ( const xmlChar* ) "name" ) == 1 ) {
						node.name = xmlStrdup( value );
					} else if ( xmlStrEqual( k, ( const xmlChar* ) "population" ) == 1 ) {
						node.population = atoi(( const char* ) value );
					} else if ( xmlStrEqual( k, ( const xmlChar* ) "highway" ) == 1 ) {
						if ( xmlStrEqual( value, ( const xmlChar* ) "traffic_signals" ) == 1 )
							node.trafficSignal = true;
					}
				}
				if ( k != NULL )
					xmlFree( k );
				if ( value != NULL )
					xmlFree( value );
			}

			xmlFree( childName );
		}
	}
	return node;
}

bool OSMImporter::SetIDMap( const std::vector< NodeID >& idMap )
{
	FileStream idMapData( fileInDirectory( m_outputDirectory, "OSM Importer" ) + "_id_map" );

	if ( !idMapData.open( QIODevice::WriteOnly ) )
		return false;

	idMapData << quint32( idMap.size() );
	for ( NodeID i = 0; i < ( NodeID ) idMap.size(); i++ )
		idMapData << quint32( idMap[i] );

	return true;
}

bool OSMImporter::GetIDMap( std::vector< NodeID >* idMap )
{
	FileStream idMapData( fileInDirectory( m_outputDirectory, "OSM Importer" ) + "_id_map" );

	if ( !idMapData.open( QIODevice::ReadOnly ) )
		return false;

	quint32 numNodes;

	idMapData >> numNodes;
	idMap->resize( numNodes );

	for ( NodeID i = 0; i < ( NodeID ) numNodes; i++ ) {
		quint32 temp;
		idMapData >> temp;
		( *idMap )[i] = temp;
	}

	if ( idMapData.status() == QDataStream::ReadPastEnd )
		return false;

	return true;
}

bool OSMImporter::GetRoutingEdges( std::vector< RoutingEdge >* data )
{
	FileStream mappedEdgesData( fileInDirectory( m_outputDirectory, "OSM Importer" ) + "_mapped_edges" );

	if ( !mappedEdgesData.open( QIODevice::ReadOnly ) )
		return false;

	unsigned wayID = 0;
	std::vector< NodeID > way;
	QString name;
	while ( true ) {
		quint32 bidirectional, numberOfPathNodes;
		mappedEdgesData >> name >> bidirectional >> numberOfPathNodes;
		if ( mappedEdgesData.status() == QDataStream::ReadPastEnd )
			break;

		way.clear();
		for ( int i = 0; i < ( int ) numberOfPathNodes; ++i ) {
			NodeID node;
			mappedEdgesData >> node;
			way.push_back( node );
		}
		for ( int i = 1; i < ( int ) numberOfPathNodes; ++i ) {
			RoutingEdge edge;
			edge.source = way[i - 1];
			edge.target = way[i];
			edge.bidirectional = bidirectional == 1;
			double seconds;
			mappedEdgesData >> seconds;
			edge.distance = seconds;

			data->push_back( edge );
		}
		wayID++;
	}

	return true;
}

bool OSMImporter::GetRoutingNodes( std::vector< RoutingNode >* data )
{
	FileStream nodeCoordinatesData( fileInDirectory( m_outputDirectory, "OSM Importer" ) + "_node_coordinates" );

	if ( !nodeCoordinatesData.open( QIODevice::ReadOnly ) )
		return false;

	while ( true ) {
		GPSCoordinate gps;
		nodeCoordinatesData >> gps.latitude >> gps.longitude;
		if ( nodeCoordinatesData.status() == QDataStream::ReadPastEnd )
			break;
		RoutingNode node;
		node.coordinate = UnsignedCoordinate( gps );
		data->push_back( node );
	}

	return true;
}

bool OSMImporter::GetAddressData( std::vector< Place >* dataPlaces, std::vector< Address >* dataAddresses, std::vector< UnsignedCoordinate >* dataWayBuffer )
{
	QString filename = fileInDirectory( m_outputDirectory, "OSM Importer" );

	FileStream mappedEdgesData( filename + "_mapped_edges" );
	FileStream nodeCoordinatesData( filename + "_node_coordinates" );
	FileStream placesData( filename + "_places" );
	FileStream locationData( filename + "_location" );

	if ( !mappedEdgesData.open( QIODevice::ReadOnly ) )
		return false;
	if ( !nodeCoordinatesData.open( QIODevice::ReadOnly ) )
		return false;
	if ( !placesData.open( QIODevice::ReadOnly ) )
		return false;
	if ( !locationData.open( QIODevice::ReadOnly ) )
		return false;

	std::vector< GPSCoordinate > coordinates;

	while ( true ) {
		GPSCoordinate gps;
		nodeCoordinatesData >> gps.latitude >> gps.longitude;
		if ( nodeCoordinatesData.status() == QDataStream::ReadPastEnd )
			break;
		coordinates.push_back( gps );
	}

	std::vector< NodeLocation > nodeLocation;
	while( true ) {
		quint32 placeID, isInPlace;
		NodeLocation location;
		locationData >> isInPlace >> placeID;

		if ( locationData.status() == QDataStream::ReadPastEnd )
			break;

		location.isInPlace = isInPlace == 1;
		location.place = placeID;
		nodeLocation.push_back( location );
	}

	while ( true ) {
		GPSCoordinate gps;
		quint32 type;
		QString name;
		quint32 population;
		placesData >> gps.latitude >> gps.longitude >> type >> population >> name;

		if ( placesData.status() == QDataStream::ReadPastEnd )
			break;

		Place place;
		place.name = name;
		place.population = population;
		place.coordinate = UnsignedCoordinate( gps );
		place.type = ( Place::Type ) type;
		dataPlaces->push_back( place );
	}

	long long numberOfWays = 0;
	long long numberOfAddressPlaces = 0;
	std::vector< NodeID > wayBuffer;

	while ( true ) {
		std::vector< NodeID > addressPlaces;
		Address newAddress;
		QString name;
		quint32 bidirectional, numberOfPathNodes;
		mappedEdgesData >> name >> bidirectional >> numberOfPathNodes;
		if ( mappedEdgesData.status() == QDataStream::ReadPastEnd )
			break;

		name = name.simplified();
		newAddress.name = name;
		newAddress.wayStart = wayBuffer.size();

		for ( int i = 0; i < ( int ) numberOfPathNodes; ++i ) {
			NodeID node;
			mappedEdgesData >> node;
			if ( name.length() > 0 ) {
				wayBuffer.push_back( node );
				if ( nodeLocation[node].isInPlace )
					addressPlaces.push_back( nodeLocation[node].place );
			}
		}
		for ( int i = 1; i < ( int ) numberOfPathNodes; ++i ) {
			double seconds;
			mappedEdgesData >> seconds;
		}

		newAddress.wayEnd = wayBuffer.size();
		numberOfWays++;

		if ( addressPlaces.size() == 0 ) {
			wayBuffer.resize( newAddress.wayStart );
			continue;
		}

		std::sort( addressPlaces.begin(), addressPlaces.end() );
		addressPlaces.resize( std::unique( addressPlaces.begin(), addressPlaces.end() ) - addressPlaces.begin() );

		if ( name.length() > 0 && addressPlaces.size() > 0 ) {
			for ( std::vector< NodeID >::const_iterator i = addressPlaces.begin(), e = addressPlaces.end(); i != e; ++i ) {
				newAddress.nearPlace = *i;
				dataAddresses->push_back( newAddress );

				numberOfAddressPlaces++;
			}
		}

	}

	dataWayBuffer->reserve( wayBuffer.size() );
	for ( std::vector< NodeID >::const_iterator i = wayBuffer.begin(), e = wayBuffer.end(); i != e; ++i ) {
		dataWayBuffer->push_back( UnsignedCoordinate( coordinates[*i] ) );
	}
	wayBuffer.clear();

	qDebug() << "OSM Importer: Number of ways:" << numberOfWays;
	qDebug() << "OSM Importer: Number of address entries:" << numberOfAddressPlaces;
	qDebug() << "OSM Importer: Average address entries per way:" << ( double ) numberOfAddressPlaces / numberOfWays;
	qDebug() << "OSM Importer: Number of way nodes:" << dataWayBuffer->size();
	return true;
}

bool OSMImporter::GetBoundingBox( BoundingBox* box )
{
	FileStream boundingBoxData( fileInDirectory( m_outputDirectory, "OSM Importer" ) + "_bounding_box" );

	if ( !boundingBoxData.open( QIODevice::ReadOnly ) )
		return false;

	GPSCoordinate minGPS, maxGPS;

	boundingBoxData >> minGPS.latitude >> minGPS.longitude >> maxGPS.latitude >> maxGPS.longitude;

	if ( boundingBoxData.status() == QDataStream::ReadPastEnd )
		return false;

	box->min = UnsignedCoordinate( minGPS );
	box->max = UnsignedCoordinate( maxGPS );
	if ( box->min.x > box->max.x )
		std::swap( box->min.x, box->max.x );
	if ( box->min.y > box->max.y )
		std::swap( box->min.y, box->max.y );

	return true;
}

void OSMImporter::DeleteTemporaryFiles()
{
	QString filename = fileInDirectory( m_outputDirectory, "OSM Importer" );
	QFile::remove( filename + "_all_nodes" );
	QFile::remove( filename + "_bounding_box" );
	QFile::remove( filename + "_city_outlines" );
	QFile::remove( filename + "_edges" );
	QFile::remove( filename + "_id_map" );
	QFile::remove( filename + "_location" );
	QFile::remove( filename + "_mapped_edges" );
	QFile::remove( filename + "_node_coordinates" );
	QFile::remove( filename + "_places" );
}

Q_EXPORT_PLUGIN2( osmimporter, OSMImporter )
