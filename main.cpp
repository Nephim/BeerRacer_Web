#include <iostream>
#include <vector>
#include <restinio/all.hpp>
#include <restinio/websocket/websocket.hpp>
#include <json_dto/pub.hpp>
#include <boost/asio.hpp>

namespace rws = restinio::websocket::basic;
using namespace boost;

asio::io_context io;
asio::serial_port port(io);

uint8_t read_from_port() 
{ 
  uint8_t c;

  asio::read(port, asio::buffer(&c,1));

  return c;
};

void write_to_port(uint8_t n)
{
  int bytes_written = asio::write(port, asio::buffer(&n, 1));

};


struct joyCords_t
{
    joyCords_t() = default;

    joyCords_t(
	   	int joyX,
		int joyY
    )
    :   m_joyX{ std::move(joyX)},
        m_joyY{ std::move(joyY)}
    {}
    template < typename JSON_IO >
    void
    json_io( JSON_IO & io )
    {
        io
            & json_dto::mandatory( "joyX", m_joyX )
            & json_dto::mandatory( "joyY", m_joyY );
    }
			
	int m_joyX;
	int m_joyY;
};

struct beer_Racer_t
{
	beer_Racer_t() = default;

	beer_Racer_t(
		std::string mode,
		int beer,
		int distance,
		joyCords_t myCords
		 )
		:	m_mode{ std::move( mode ) }
		,	m_beer{ std::move( beer ) }
		,	m_distance{ std::move( distance ) }
		,	m_cords{ std::move( myCords ) }
	{}

	template < typename JSON_IO >
	void
	json_io( JSON_IO & io )
	{
		io
			& json_dto::mandatory( "mode", m_mode )
			& json_dto::mandatory( "beer", m_beer )
			& json_dto::mandatory( "distance", m_distance )
			& json_dto::mandatory( "cords", m_cords );
	}

	std::string m_mode;
    int m_beer;
	int m_distance;
	joyCords_t m_cords;
};

using beer_Racer_data_t = std::vector< beer_Racer_t >;

namespace rr = restinio::router;
using router_t = rr::express_router_t<>;

using traits_t = restinio::traits_t< restinio::asio_timer_manager_t, restinio::single_threaded_ostream_logger_t, router_t >;
using ws_registry_t = std::map< std::uint64_t, rws::ws_handle_t >;



class beer_Racer__data_handler_t
{
public :
	explicit beer_Racer__data_handler_t( beer_Racer_data_t & rData )
		:	m_racerData( rData )
	{}

	beer_Racer__data_handler_t( const beer_Racer__data_handler_t & ) = delete;
	beer_Racer__data_handler_t( beer_Racer__data_handler_t && ) = delete;

	//get der henter all data. Almindelig get.
	auto on_rData_list(
		const restinio::request_handle_t& req, rr::route_params_t ) const
	{
		auto resp = init_resp( req->create_response() );

		resp.set_body(json_dto::to_json(m_racerData));

		return resp.done();
	}

	//BIL API, skal ændre settings til slow mode 0 eller sonic mode 1.

	auto on_update_mode(
		const restinio::request_handle_t& req, rr::route_params_t params )
	{
		auto resp = init_resp( req->create_response() );
		try
		{
			
			sendMessage("Mode changed");

					if(m_racerData[ 0 ].m_mode == "Snail")
					{
						m_racerData[ 0 ].m_mode = "Ferrari";
						write_to_port(0b11100000);
					}
					else
					{
						m_racerData[ 0 ].m_mode = "Snail";
						write_to_port(0b11100001);
					}

			return resp.done();
		}
		catch( const std::exception & /*ex*/ )
		{
			mark_as_bad_request( resp );

		}
		resp.set_body( "Mode mistake \n" );
		return resp.done();
	}

	auto on_joy_update(

		const restinio::request_handle_t& req, rr::route_params_t params )
	{
	
		auto resp = init_resp( req->create_response() );

		try
		{
			sendMessage("Updated XY");

			auto b = json_dto::from_json< joyCords_t >( req->body() );

					m_racerData[ 0 ].m_cords.m_joyX = b.m_joyX ;
					m_racerData[ 0 ].m_cords.m_joyY = b.m_joyY ;

					uint8_t tempX = 0b00000000;
					uint8_t tempY = 0b00000000;

					if(b.m_joyY > 0)
					{
						tempY = (uint8_t)b.m_joyY;
						tempY = 0b00100000+tempY;
					}
					else if (b.m_joyY < 0)
					{
						uint8_t toPositive = (b.m_joyY * -1);
						tempY = 0b00110000+toPositive;
					}
					else
					{
						tempY = 0b01000000;
					}

					write_to_port(tempY);

					if(b.m_joyX > 5)
					{
						tempX = 0b01100000;
					}
					else if (b.m_joyX < -5)
					{
						tempX = 0b01100001;
					}
					else
					{
						tempX = 0b10000000;
					}
					write_to_port(tempX);


			return resp.done();
		}
		catch( const std::exception & /*ex*/ )
		{
			mark_as_bad_request( resp );

		}
		resp.set_body( "Joy mistake \n" );
		return resp.done();
	}


// Server kode der virker med CORS. bruges til HTML siden.
	auto on_live_update(const restinio::request_handle_t& req, rr::route_params_t params)
	{
		if (restinio::http_connection_header_t::upgrade == req->header().connection())
		{
			auto wsh = rws::upgrade<traits_t>(
			*req, rws::activation_t::immediate,
			[this](auto wsh, auto m)
			{
				if (rws::opcode_t::text_frame == m->opcode() ||
				rws::opcode_t::binary_frame == m->opcode() ||
				rws::opcode_t::continuation_frame == m->opcode())
				{
					wsh->send_message(*m);				
				}
			else if (rws::opcode_t::ping_frame == m->opcode())
				{
					auto resp = *m;
					resp.set_opcode(rws::opcode_t::pong_frame);
					wsh->send_message(resp);
				}	
			else if (rws::opcode_t::connection_close_frame == m->opcode())
				{
					m_registry.erase(wsh->connection_id());
				}
			});
			m_registry.emplace(wsh->connection_id(), wsh);
			init_resp(req->create_response()).done();
			return restinio::request_accepted();
		}
	return restinio::request_rejected();
	}	
//copy paste fra slides til HTML og cors.
	auto options(restinio::request_handle_t req, restinio::router::route_params_t)
	{
		const auto methods = "OPTIONS, GET, POST, PATCH, DELETE, PUT";
		auto resp = init_resp(req->create_response());
		resp.append_header(restinio::http_field::access_control_allow_methods, methods);
		resp.append_header(restinio::http_field::access_control_allow_headers, "content-type");
		resp.append_header(restinio::http_field::access_control_max_age, "864000");
		return resp.done();
	}

private :
	beer_Racer_data_t & m_racerData;

	template < typename RESP >
	static RESP
	init_resp( RESP resp )
	{
		resp
		.append_header( "Server", "RESTinio sample server /v.0.6" )
		.append_header_date_field()
		.append_header( "Content-Type", "text/plain; charset=utf-8" )
		.append_header(restinio::http_field::access_control_allow_origin, "*");

		return resp;
	}

	template < typename RESP >
	static void
	mark_as_bad_request( RESP & resp )
	{
		resp.header().status_line( restinio::status_bad_request() );
	}

	ws_registry_t m_registry;
	void sendMessage(std::string message)
		{
			for(auto [k, v] : m_registry)
			v-> send_message(rws::final_frame, rws::opcode_t::text_frame, message);
		}

};

auto server_handler( beer_Racer_data_t & rData_collection )
{
	auto router = std::make_unique< router_t >();
	auto handler = std::make_shared< beer_Racer__data_handler_t >( std::ref(rData_collection) );

	auto by = [&]( auto method ) {
		using namespace std::placeholders;
		return std::bind( method, handler, _1, _2 );
	};

	auto method_not_allowed = []( const auto & req, auto ) {
			return req->create_response( restinio::status_method_not_allowed() )
					.connection_close()
					.done();
		};


	// handler til coers serverside
	router->http_get("/chat", by(&beer_Racer__data_handler_t::on_live_update));


	router->add_handler(restinio::http_method_options(), "/", by(&beer_Racer__data_handler_t::options));
	router->add_handler(restinio::http_method_options(), "/:mode", by(&beer_Racer__data_handler_t::options));



	// Handlers for '/' path.
	router->http_get( "/", by( &beer_Racer__data_handler_t::on_rData_list ) );
	router->http_put("/", by( &beer_Racer__data_handler_t::on_update_mode ) );
	

	// Disable all other methods for '/'.
	router->add_handler(
	 		restinio::router::none_of_methods(
	 				restinio::http_method_get(), restinio::http_method_put() ),
	 		"/", method_not_allowed );

	

	// Handlers for '/:ID' path.
		router->http_put("/:mode",
	 		by( &beer_Racer__data_handler_t::on_joy_update ) );

	// Disable all other methods for '/:ID'.
	router->add_handler(
			restinio::router::none_of_methods(
					restinio::http_method_post()),
			"/:mode", method_not_allowed );

	return router;
}

void* reader(void* arg)
{
	beer_Racer_data_t* tmpData_collection = (beer_Racer_data_t*)arg;


    while(true)
    {
        uint8_t n = read_from_port();
        
		if (n < 10)
		{
			(*tmpData_collection)[0].m_beer = n;
		}
		else if (n >= 128)
		{
			(*tmpData_collection)[0].m_distance = (n - 128);
		}
    }
}

int main()
{

	port.open("/dev/ttyS0"); // Hvis i bruger en USB2Uart ting
    port.set_option(asio::serial_port_base::baud_rate(115200));
	pthread_t   readUART;


	using namespace std::chrono;

	try
	{
		using traits_t =
			restinio::traits_t<
				restinio::asio_timer_manager_t,
				restinio::single_threaded_ostream_logger_t,
				router_t >;

		joyCords_t startCords{0, 0};

		beer_Racer_data_t rData_collection{
			{"Ferrari", 0, 0, startCords},
		};

  		pthread_create(&readUART, NULL, reader, &rData_collection);


		restinio::run(
			restinio::on_this_thread< traits_t >()
				.address( "0.0.0.0" )
				.request_handler( server_handler( rData_collection ) )
				.read_next_http_message_timelimit( 10s )
				.write_http_response_timelimit( 1s )
				.handle_request_timeout( 1s ) );
	}
	catch( const std::exception & ex )
	{
		std::cerr << "Error: " << ex.what() << std::endl;
		return 1;
	}

	write_to_port(0b01000000);

	port.close();

	return 0;
}
