// ReadState state = AWAITING_INPUT_PACKETS;
AVFrame *frame;
int n_decoded_frames = 0;
int n_demuxed_frames = 0;
int frames_in_interval = 0;
steady_clock::time_point fps_interval_start = steady_clock::now();

while (state != BREAK) {
    switch (state) {
        case ERROR: {
            fprintf(stderr, "Error %d\n", ret);
            state = BREAK;
            break;
        }
        case INPUT_ENDED: {
            fprintf(stderr, "Processing complete!\n");
            state = BREAK;
            break;
        }
        case AWAITING_INPUT_PACKETS: {
            ret = av_read_frame(ioContext->format_ctx, &packet);
            if (ret < 0) {
                state = INPUT_ENDED;
                break;
            }

            AVStream *stream = ioContext->format_ctx->streams[packet.stream_index];
            if (packet.stream_index == ioContext->video_stream) {
                std::cerr << "Video frame number: " << n_demuxed_frames << "\n";
                n_demuxed_frames++;
                std::cerr << "Video frame presentation: " <<
                    1.0 * packet.pts * stream->time_base.num / stream->time_base.den <<
                   " (" << packet.pts << " * " << stream->time_base.num <<
                    " / " << stream->time_base.den << ")\n";
                std::cerr << "Video frame duration: " <<
                    1.0 * packet.duration * stream->time_base.num / stream->time_base.den <<
                   " (" << packet.duration << " * " << stream->time_base.num <<
                    " / " << stream->time_base.den << ")\n";
                // fprintf(
                //     stderr,
                //     "Read video packet at %lf seconds\n",
                //     1.0 * packet.pts * stream->time_base.num / stream->time_base.den
                // );
                // ret = avcodec_send_packet(ioContext->decoder_ctx, &packet);
                // if (ret) {
                //     state = ERROR;
                // }
            } else if (packet.stream_index == ioContext->gpmf_stream) {
                double pt_timestamp = 1.0 * packet.pts * stream->time_base.num / stream->time_base.den;
                double pt_duration = 1.0 * packet.duration * stream->time_base.num / stream->time_base.den;
                std::cerr << "GPMF frame presentation: " <<
                    1.0 * packet.pts * stream->time_base.num / stream->time_base.den <<
                    " (" << packet.pts << " * " << stream->time_base.num <<
                    " / " << stream->time_base.den << ")\n";
                std::cerr << "GPMF frame duration: " <<
                    1.0 * packet.duration * stream->time_base.num / stream->time_base.den <<
                   " (" << packet.duration << " * " << stream->time_base.num <<
                    " / " << stream->time_base.den << ")\n";
                ret = process_sensor_data(
                    ioContext,
                    (uint32_t *) packet.data,
                    packet.size,
                    pt_timestamp,
                    pt_duration
                );
                if (ret) {
                    state = ERROR;
                }
            } else {
                // Ignore audio packets, etc
            }

            av_packet_unref(&packet);
            if (state == AWAITING_INPUT_PACKETS) {
                state = AWAITING_INPUT_FRAMES;
            }
            break;
        }
        case AWAITING_INPUT_FRAMES: {
            frame = av_frame_alloc();
            ret = avcodec_receive_frame(ioContext->decoder_ctx, frame);
            if (!ret) {
                ret = process_frame(ioContext, frame);
                if (ret) {
                    fprintf(stderr, "Failed to process frame\n");
                    state = ERROR;
                    break;
                }

                // success
                n_decoded_frames++;
                frames_in_interval++;
                milliseconds ms_in_interval = duration_cast<milliseconds>(
                    steady_clock::now() - fps_interval_start
                );
                if (ms_in_interval.count() > 1000) {
                    fprintf(
                        stderr,
                        "Decoded %d frames (%.0lffps)\n",
                        n_decoded_frames,
                        frames_in_interval * 1000.0 / ms_in_interval.count()
                    );
                    frames_in_interval = 0;
                    fps_interval_start = steady_clock::now();
                }
            } else if (ret == AVERROR(EAGAIN)) {
                state = AWAITING_INPUT_PACKETS;
            } else if (state == AVERROR_EOF) {
                state = INPUT_ENDED;
            } else if (ret) {
                state = ERROR;
            }
            av_frame_free(&frame);
            break;
        }
        case BREAK: {
            break;
        }
    }
}