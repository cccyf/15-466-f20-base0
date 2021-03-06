#include "PongMode.hpp"

//for the GL_ERRORS() macro:
#include "gl_errors.hpp"

//for glm::value_ptr() :
#include <glm/gtc/type_ptr.hpp>
#include <math.h>
#include <random>

PongMode::PongMode() {

	//set up trail as if ball has been here for 'forever':
	ball_trail.clear();
	// Construct and insert element at the end
	ball_trail.emplace_back(ball, trail_length);
	ball_trail.emplace_back(ball, 0.0f);

	
	//----- allocate OpenGL resources -----
	{ //vertex buffer:
		glGenBuffers(1, &vertex_buffer); // a name, like a pointer
		//for now, buffer will be un-filled.

		GL_ERRORS(); //PARANOIA: print out any OpenGL errors that may have happened
	}

	{ //vertex array mapping buffer for color_texture_program:
		//ask OpenGL to fill vertex_buffer_for_color_texture_program with the name of an unused vertex array object:
		glGenVertexArrays(1, &vertex_buffer_for_color_texture_program);

		//set vertex_buffer_for_color_texture_program as the current vertex array object:
		glBindVertexArray(vertex_buffer_for_color_texture_program);

		//set vertex_buffer as the source of glVertexAttribPointer() commands:
		glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer);
		// type, name
		// data of this type -> go to the name

		//set up the vertex array object to describe arrays of PongMode::Vertex:
		glVertexAttribPointer(//todo
			color_texture_program.Position_vec4, //attribute
			3, //size
			GL_FLOAT, //type
			GL_FALSE, //normalized
			sizeof(Vertex), //stride
			(GLbyte *)0 + 0 //offset
		);
		// open this buffer object
		glEnableVertexAttribArray(color_texture_program.Position_vec4);
		//[Note that it is okay to bind a vec3 input to a vec4 attribute -- the w component will be filled with 1.0 automatically]

		glVertexAttribPointer(
			color_texture_program.Color_vec4, //attribute
			4, //size
			GL_UNSIGNED_BYTE, //type
			GL_TRUE, //normalized
			sizeof(Vertex), //stride
			(GLbyte *)0 + 4*3 //offset
		);
		glEnableVertexAttribArray(color_texture_program.Color_vec4);

		glVertexAttribPointer(
			color_texture_program.TexCoord_vec2, //attribute
			2, //size
			GL_FLOAT, //type
			GL_FALSE, //normalized
			sizeof(Vertex), //stride
			(GLbyte *)0 + 4*3 + 4*1 //offset
		);
		glEnableVertexAttribArray(color_texture_program.TexCoord_vec2);

		// TODO
		//done referring to vertex_buffer, so unbind it:
		glBindBuffer(GL_ARRAY_BUFFER, 0);

		//done setting up vertex array object, so unbind it:
		glBindVertexArray(0);

		GL_ERRORS(); //PARANOIA: print out any OpenGL errors that may have happened
	}

	{ //solid white texture:
		//ask OpenGL to fill white_tex with the name of an unused texture object:
		glGenTextures(1, &white_tex);

		//bind that texture object as a GL_TEXTURE_2D-type texture:
		glBindTexture(GL_TEXTURE_2D, white_tex);

		//upload a 1x1 image of solid white to the texture:
		glm::uvec2 size = glm::uvec2(1,1);
		// glm u8vec4 color
		std::vector< glm::u8vec4 > data(size.x*size.y, glm::u8vec4(0xff, 0xff, 0xff, 0xff));
		// generate a 2d texture
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, size.x, size.y, 0, GL_RGBA, GL_UNSIGNED_BYTE, data.data());

		//set filtering and wrapping parameters:
		//(it's a bit silly to mipmap a 1x1 texture, but I'm doing it because you may want to use this code to load different sizes of texture)
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		// s-x, t-y
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

		//since texture uses a mipmap and we haven't uploaded one, instruct opengl to make one for us:
		glGenerateMipmap(GL_TEXTURE_2D);

		//Okay, texture uploaded, can unbind it:
		glBindTexture(GL_TEXTURE_2D, 0);

		GL_ERRORS(); //PARANOIA: print out any OpenGL errors that may have happened
	}
}

PongMode::~PongMode() {

	//----- free OpenGL resources -----
	glDeleteBuffers(1, &vertex_buffer);
	vertex_buffer = 0;

	glDeleteVertexArrays(1, &vertex_buffer_for_color_texture_program);
	vertex_buffer_for_color_texture_program = 0;

	glDeleteTextures(1, &white_tex);
	white_tex = 0;
}

bool PongMode::handle_event(SDL_Event const &evt, glm::uvec2 const &window_size) {

	if (evt.type == SDL_MOUSEMOTION) {
		//convert mouse from window pixels (top-left origin, +y is down) to clip space ([-1,1]x[-1,1], +y is up):
		glm::vec2 clip_mouse = glm::vec2( // todo
			(evt.motion.x + 0.5f) / window_size.x * 2.0f - 1.0f,
			(evt.motion.y + 0.5f) / window_size.y *-2.0f + 1.0f
		);
		left_paddle.y = (clip_to_court * glm::vec3(clip_mouse, 1.0f)).y;
		left_paddle.x = (clip_to_court * glm::vec3(clip_mouse, 1.0f)).x;
	}

	return false;
}

void PongMode::update(float elapsed) {

	static std::mt19937 mt; //mersenne twister pseudo-random number generator

	//----- paddle update -----

    // set position inside of the court
	left_paddle.y = std::max(left_paddle.y, -court_radius.y + paddle_radius.y);
	left_paddle.y = std::min(left_paddle.y,  court_radius.y - paddle_radius.y);

	//----- ball update -----

	//speed of ball doubles every four points:
	float speed_multiplier = 4.0f * std::pow(2.0f, (left_score + right_score) / 4.0f);

	//velocity cap, though (otherwise ball can pass through paddles):
	speed_multiplier = std::min(speed_multiplier, 10.0f);

	ball += elapsed * speed_multiplier * ball_velocity;

	//---- collision handling ----

	//paddles:
	auto paddle_vs_ball = [this](glm::vec2 const &paddle) {
		//compute area of overlap:
		glm::vec2 min = glm::max(paddle - paddle_radius, ball - ball_radius);
		glm::vec2 max = glm::min(paddle + paddle_radius, ball + ball_radius);

		//if no overlap, no collision:
		if (min.x > max.x || min.y > max.y) return;

		if (max.x - min.x > max.y - min.y) {
			//wider overlap in x => bounce in y direction:
			if (ball.y > paddle.y) {
				ball.y = paddle.y + paddle_radius.y + ball_radius.y;
				ball_velocity.y = std::abs(ball_velocity.y);
			} else {
				ball.y = paddle.y - paddle_radius.y - ball_radius.y;
				ball_velocity.y = -std::abs(ball_velocity.y);
			}
		} else {
			//wider overlap in y => bounce in x direction:
			if (ball.x > paddle.x) {
				ball.x = paddle.x + paddle_radius.x + ball_radius.x;
				ball_velocity.x = std::abs(ball_velocity.x);
			} else {
				ball.x = paddle.x - paddle_radius.x - ball_radius.x;
				ball_velocity.x = -std::abs(ball_velocity.x);
			}
			//warp y velocity based on offset from paddle center:
			float vel = (ball.y - paddle.y) / (paddle_radius.y + ball_radius.y);
			ball_velocity.y = glm::mix(ball_velocity.y, vel, 0.75f);
		}
	};
	paddle_vs_ball(left_paddle);
//	paddle_vs_ball(right_paddle);

	//court walls:

	if (ball.y > court_radius.y) {
		ball.y = -court_radius.y + ball_radius.y;
		ball_velocity.y = 2.0f;
	}else if (ball.y < -court_radius.y) {
		ball.y = court_radius.y - ball_radius.y;
		ball_velocity.y = 0;
	}else if (ball.x > court_radius.x - ball_radius.x) {
		ball.x = court_radius.x - ball_radius.x;
		if (ball_velocity.x > 0.0f) {
			ball_velocity.x = -ball_velocity.x;
		}
	}else if (ball.x < -court_radius.x + ball_radius.x) {
		ball.x = -court_radius.x + ball_radius.x;
		if (ball_velocity.x < 0.0f) {
			ball_velocity.x = -ball_velocity.x;
		}
	}

	// if hit
	// for
	auto it = target_set.begin();
	while(it!=target_set.end()){
		if (it->second.first.x < target_radius_max){
			it++;
			continue;
		}
		
		auto it_pos = it->first;
		if (ball.y + ball_radius.y >= it_pos.y - target_radius_max && ball.y - ball_radius.y <= it_pos.y + target_radius_max
			&& ball.x - ball_radius.x <= it_pos.x + target_radius_max && ball.x + ball_radius.x >= it_pos.x - target_radius_max){
			target_disappearing_set.insert(Target(it->first, it->second));
			it = target_set.erase(it);
			target_count -= 1;
			left_score += 1;
			
			ball.y = court_radius.y - ball_radius.y;
			ball.x = -court_radius.x + (float) (rand() % 100) / 100.0f * (2 * (court_radius.x - ball_radius.x));
			
			ball_velocity.y = 0.f;
			ball_velocity.x = 0.5f;

			continue;
		}
		it++;
	}
	

	// add grativity
	ball_velocity.y -= elapsed * 2;

	// target
	if (target_count < target_count_max){
		float x = - target_court_radius.x + (float) (rand() % 100) / 100.0f * (2 * target_court_radius.x);
		float y = - target_court_radius.y + (float) (rand() % 100) / 100.0f * (2 * target_court_radius.y);
		int rand_pos = rand()%10;
		target_set.insert(Target(glm::vec2(x, y), std::pair<glm::vec2, int>(glm::vec2(0.f, 0.f), rand_pos)));
		target_count += 1;
	}

	std::set<Target, TargetCompare> updated;
	for (it = target_set.begin(); it!=target_set.end(); it++){
		glm::vec2 it_radius = it->second.first;
		if (it_radius.x >= target_radius_max){
			updated.insert(Target(it->first, it->second));
			continue;
		}
		updated.insert(Target(it->first, 
		std::pair<glm::vec2, int>(glm::vec2(it_radius.x + elapsed / enlarge_duration_limit * target_radius_max,
					it_radius.y + elapsed / enlarge_duration_limit * target_radius_max),
					it->second.second)));
	}

	target_set.clear();
	for (auto &t: updated){
		target_set.insert(t);
	}

	updated.clear();

	for(it = target_disappearing_set.begin(); it !=target_disappearing_set.end(); it++){
		glm::vec2 it_radius = it->second.first;
		if (it_radius.x <= 0){
			continue;
		}
		updated.insert(Target(it->first, std::pair<glm::vec2, int>(glm::vec2(
		it_radius.x - elapsed / disappear_duration_limit * target_radius_max,
		it_radius.y - elapsed / disappear_duration_limit * target_radius_max), it->second.second)));
	}

	target_disappearing_set.clear();
	for (auto &t: updated){
		target_disappearing_set.insert(t);
	}



	//----- rainbow trails -----

	//age up all locations in ball trail:
	for (auto &t : ball_trail) {
		t.z += elapsed;
	}
	//store fresh location at back of ball trail:
	ball_trail.emplace_back(ball, 1.0f);

	//trim any too-old locations from back of trail:
	//NOTE: since trail drawing interpolates between points, only removes back element if second-to-back element is too old:
	while (ball_trail.size() >= 2 && ball_trail[1].z > trail_length) {
		ball_trail.pop_front();
	}
}

void PongMode::draw(glm::uvec2 const &drawable_size) {
	//some nice colors from the course web page:
	#define HEX_TO_U8VEC4( HX ) (glm::u8vec4( (HX >> 24) & 0xff, (HX >> 16) & 0xff, (HX >> 8) & 0xff, (HX) & 0xff ))
	// const glm::u8vec4 bg_color = HEX_TO_U8VEC4(0x171714ff);
	const glm::u8vec4 bg_color = HEX_TO_U8VEC4(0xf7f2ebff);
	// const glm::u8vec4 fg_color = HEX_TO_U8VEC4(0xd1bb54ff);
	const glm::u8vec4 fg_color = HEX_TO_U8VEC4(0x38a3a5ff);
	// const glm::u8vec4 target_color = HEX_TO_U8VEC4(0xdb717688);
	const glm::u8vec4 ball_color = HEX_TO_U8VEC4(0x38a3a5ff);
	const glm::u8vec4 shadow_color = HEX_TO_U8VEC4(0x878580ff);
	const std::vector< glm::u8vec4 > rainbow_colors = {
		HEX_TO_U8VEC4(0xff595eff), HEX_TO_U8VEC4(0xffca3aff), HEX_TO_U8VEC4(0xf8961eff),
		HEX_TO_U8VEC4(0x908e6dff), HEX_TO_U8VEC4(0xf9c74fff), HEX_TO_U8VEC4(0x90be6dff),
		HEX_TO_U8VEC4(0x43aa8bff), HEX_TO_U8VEC4(0xd64045ff), HEX_TO_U8VEC4(0x8ac926ff),
		HEX_TO_U8VEC4(0x6f7092ff), 
	};
	#undef HEX_TO_U8VEC4

	//other useful drawing constants:
	const float wall_radius = 0.05f;
	const float shadow_offset = 0.07f;
	const float padding = 0.14f; //padding between outside of walls and edge of window

	//---- compute vertices to draw ----

	//vertices will be accumulated into this list and then uploaded+drawn at the end of this function:
	std::vector< Vertex > vertices;

	//inline helper function for rectangle drawing:
	auto draw_rectangle = [&vertices](glm::vec2 const &center, glm::vec2 const &radius, glm::u8vec4 const &color) {
		//draw rectangle as two CCW-oriented triangles: counterclockwise
		// right bottom
		vertices.emplace_back(glm::vec3(center.x-radius.x, center.y-radius.y, 0.0f), color, glm::vec2(0.5f, 0.5f));
		vertices.emplace_back(glm::vec3(center.x+radius.x, center.y-radius.y, 0.0f), color, glm::vec2(0.5f, 0.5f));
		vertices.emplace_back(glm::vec3(center.x+radius.x, center.y+radius.y, 0.0f), color, glm::vec2(0.5f, 0.5f));

		// left top
		vertices.emplace_back(glm::vec3(center.x-radius.x, center.y-radius.y, 0.0f), color, glm::vec2(0.5f, 0.5f));
		vertices.emplace_back(glm::vec3(center.x+radius.x, center.y+radius.y, 0.0f), color, glm::vec2(0.5f, 0.5f));
		vertices.emplace_back(glm::vec3(center.x-radius.x, center.y+radius.y, 0.0f), color, glm::vec2(0.5f, 0.5f));
	};

	auto draw_circle = [&vertices](glm::vec2 const &center, float const &radius, glm::u8vec4 const &color) {
		for (int a = 0; a < 360; a+=1){
			vertices.emplace_back(glm::vec3(center, 0.0f), color, glm::vec2(0.5f, 0.5f));
			vertices.emplace_back(glm::vec3(center.x + radius*cos(a), center.y + radius*sin(a), 0.0f), color, glm::vec2(0.5f, 0.5f));
			vertices.emplace_back(glm::vec3(center.x + radius*cos(a), center.y + radius*sin(a), 0.0f), color, glm::vec2(0.5f, 0.5f));
			vertices.emplace_back(glm::vec3(center.x + radius*cos(a), center.y - radius*sin(a), 0.0f), color, glm::vec2(0.5f, 0.5f));
			
			vertices.emplace_back(glm::vec3(center, 0.0f), color, glm::vec2(0.5f, 0.5f));
			vertices.emplace_back(glm::vec3(center.x + radius*cos(a), center.y + radius*sin(a), 0.0f), color, glm::vec2(0.5f, 0.5f));
			vertices.emplace_back(glm::vec3(center.x - radius*cos(a), center.y + radius*sin(a), 0.0f), color, glm::vec2(0.5f, 0.5f));
		}
	};

	auto it = target_set.begin();
	while (it!=target_set.end()){
		draw_circle(it->first, it->second.first.x*2, rainbow_colors[it->second.second]);
		draw_circle(it->first, it->second.first.x, bg_color);
		it++;
	}

	it = target_disappearing_set.begin();
	while (it!=target_disappearing_set.end()){
		draw_circle(it->first, it->second.first.x*2, rainbow_colors[it->second.second]);
		draw_circle(it->first, it->second.first.x, bg_color);
		it++;
	}

	//shadows for everything (except the trail):

	glm::vec2 s = glm::vec2(0.0f,-shadow_offset);

	draw_rectangle(glm::vec2(-court_radius.x-wall_radius, 0.0f)+s, glm::vec2(wall_radius, court_radius.y + 2.0f * wall_radius), shadow_color);
	draw_rectangle(glm::vec2( court_radius.x+wall_radius, 0.0f)+s, glm::vec2(wall_radius, court_radius.y + 2.0f * wall_radius), shadow_color);
	draw_rectangle(glm::vec2( 0.0f,-court_radius.y-wall_radius)+s, glm::vec2(court_radius.x, wall_radius), shadow_color);
	draw_rectangle(glm::vec2( 0.0f, court_radius.y+wall_radius)+s, glm::vec2(court_radius.x, wall_radius), shadow_color);
	draw_rectangle(left_paddle+s, paddle_radius, shadow_color);
//	draw_rectangle(right_paddle+s, paddle_radius, shadow_color);
	
	draw_circle(ball+s, ball_radius.x, shadow_color);

	//walls:
	draw_rectangle(glm::vec2(-court_radius.x-wall_radius, 0.0f), glm::vec2(wall_radius, court_radius.y + 2.0f * wall_radius), fg_color);
	draw_rectangle(glm::vec2( court_radius.x+wall_radius, 0.0f), glm::vec2(wall_radius, court_radius.y + 2.0f * wall_radius), fg_color);
	draw_rectangle(glm::vec2( 0.0f,-court_radius.y-wall_radius), glm::vec2(court_radius.x, wall_radius), fg_color);
	draw_rectangle(glm::vec2( 0.0f, court_radius.y+wall_radius), glm::vec2(court_radius.x, wall_radius), fg_color);

	//paddles:
	draw_rectangle(left_paddle, paddle_radius, ball_color);
	

	//ball:
	draw_circle(ball, ball_radius.x, ball_color);

	//scores:
	glm::vec2 score_radius = glm::vec2(0.1f, 0.1f);
	for (uint32_t i = 0; i < left_score; ++i) {
		draw_rectangle(glm::vec2( -court_radius.x + (2.0f + 3.0f * i) * score_radius.x, court_radius.y + 2.0f * wall_radius + 2.0f * score_radius.y), score_radius, fg_color);
	}
	for (uint32_t i = 0; i < right_score; ++i) {
		draw_rectangle(glm::vec2( court_radius.x - (2.0f + 3.0f * i) * score_radius.x, court_radius.y + 2.0f * wall_radius + 2.0f * score_radius.y), score_radius, fg_color);
	}



	//------ compute court-to-window transform ------

	//compute area that should be visible:
	glm::vec2 scene_min = glm::vec2(
		-court_radius.x - 2.0f * wall_radius - padding,
		-court_radius.y - 2.0f * wall_radius - padding
	);
	glm::vec2 scene_max = glm::vec2(
		court_radius.x + 2.0f * wall_radius + padding,
		court_radius.y + 2.0f * wall_radius + 3.0f * score_radius.y + padding
	);

	//compute window aspect ratio:
	float aspect = drawable_size.x / float(drawable_size.y);
	//we'll scale the x coordinate by 1.0 / aspect to make sure things stay square.

	//compute scale factor for court given that...
	float scale = std::min(
		(2.0f * aspect) / (scene_max.x - scene_min.x), //... x must fit in [-aspect,aspect] ...
		(2.0f) / (scene_max.y - scene_min.y) //... y must fit in [-1,1].
	);

	glm::vec2 center = 0.5f * (scene_max + scene_min);

	//build matrix that scales and translates appropriately:
	glm::mat4 court_to_clip = glm::mat4(
		glm::vec4(scale / aspect, 0.0f, 0.0f, 0.0f),
		glm::vec4(0.0f, scale, 0.0f, 0.0f),
		glm::vec4(0.0f, 0.0f, 1.0f, 0.0f),
		glm::vec4(-center.x * (scale / aspect), -center.y * scale, 0.0f, 1.0f)
	);
	//NOTE: glm matrices are specified in *Column-Major* order,
	// so each line above is specifying a *column* of the matrix(!)

	//also build the matrix that takes clip coordinates to court coordinates (used for mouse handling):
	clip_to_court = glm::mat3x2(
		glm::vec2(aspect / scale, 0.0f),
		glm::vec2(0.0f, 1.0f / scale),
		glm::vec2(center.x, center.y)
	);

	//---- actual drawing ----

	//clear the color buffer:
	glClearColor(bg_color.r / 255.0f, bg_color.g / 255.0f, bg_color.b / 255.0f, bg_color.a / 255.0f);
	glClear(GL_COLOR_BUFFER_BIT);

	//use alpha blending:
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	//don't use the depth test:
	glDisable(GL_DEPTH_TEST);

	//upload vertices to vertex_buffer:
	glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer); //set vertex_buffer as current
	glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(vertices[0]), vertices.data(), GL_STREAM_DRAW); //upload vertices array
	glBindBuffer(GL_ARRAY_BUFFER, 0);

	//set color_texture_program as current program:
	glUseProgram(color_texture_program.program);

	//upload OBJECT_TO_CLIP to the proper uniform location:
	glUniformMatrix4fv(color_texture_program.OBJECT_TO_CLIP_mat4, 1, GL_FALSE, glm::value_ptr(court_to_clip));

	//use the mapping vertex_buffer_for_color_texture_program to fetch vertex data:
	glBindVertexArray(vertex_buffer_for_color_texture_program);

	//bind the solid white texture to location zero so things will be drawn just with their colors:
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, white_tex);

	//run the OpenGL pipeline:
	glDrawArrays(GL_TRIANGLES, 0, GLsizei(vertices.size()));

	//unbind the solid white texture:
	glBindTexture(GL_TEXTURE_2D, 0);

	//reset vertex array to none:
	glBindVertexArray(0);

	//reset current program to none:
	glUseProgram(0);
	

	GL_ERRORS(); //PARANOIA: print errors just in case we did something wrong.

}
