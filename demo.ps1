#######################################################################
# This is a Windows PowerShell script
#
# (-) Install icpp-pro or icpp-free in a python environment on windows
# (-) Install dfx  in wsl
# (-) In a Windows PowerShell (*):
#
#     .\demo.ps1
#
# (*) The Miniconda Powershell is highly recommended
#
#######################################################################
Write-Host " "
Write-Host "--------------------------------------------------"
Write-Host "Stopping the local network in wsl"
wsl dfx stop

Write-Host " "
Write-Host "--------------------------------------------------"
Write-Host "Starting the local network in wsl as a PowerShell background job"
$jobName = "llama_cpp"

# Stop the job if it already exist
# Get a list of all the background jobs with a specific name
$jobs = Get-Job -Name $jobName -ErrorAction SilentlyContinue

# Stop all the jobs
$jobs | Remove-Job

# Start the local network with redirecting error messages to output stream
$job = Start-Job -ScriptBlock { wsl dfx start --clean 2>&1 } -Name $jobName

# Display the details of the job
$job | Format-Table

# Wait for 10 seconds
Write-Host " "
Write-Host "Waiting for 10 seconds to allow the local network to start..."
Start-Sleep -Seconds 10

# Get the output of the job
$output = Receive-Job -Job $job
Write-Host $output -ForegroundColor Green

#######################################################################
Write-Host " "
Write-Host "--------------------------------------------------"
Write-Host "Building the wasm with wasi-sdk"
icpp build-wasm --to-compile all
# icpp build-wasm --to-compile mine-no-lib

#######################################################################
Write-Host " "
Write-Host "--------------------------------------------------"
Write-Host "Deploying the wasm to a canister on the local network"
wsl --% . ~/.local/share/dfx/env; dfx deploy

#######################################################################
echo " "
echo "--------------------------------------------------"
echo "Uploading the *.gguf model file"
python -m scripts.upload models/stories260Ktok512.gguf
# python -m scripts.upload models/stories15Mtok4096.gguf
# python -m scripts.upload ../../repos_hf/Phi-3-mini-4k-instruct-gguf/Phi-3-mini-4k-instruct-q4.gguf --canister-file models/Phi-3-mini-4k-instruct-q4.gguf

#######################################################################
Write-Host " "
Write-Host "--------------------------------------------------"
Write-Host "Running some manual tests with dfx"
wsl --% . ~/.local/share/dfx/env; dfx canister call llama_cpp run_query '(record { args = vec {"--model"; "models/stories260Ktok512.gguf"; "--prompt"; "Patrick loves ice-cream. On a hot day "; "--n-predict"; "25"; "--ctx-size"; "128"} })'
wsl --% . ~/.local/share/dfx/env; dfx canister call llama_cpp run_update '(record { args = vec {"--model"; "models/stories260Ktok512.gguf"; "--prompt"; "Patrick loves ice-cream. On a hot day "; "--n-predict"; "25"; "--ctx-size"; "128"} })'

#######################################################################
Write-Host " "
Write-Host "--------------------------------------------------"
Write-Host "Running the full smoketests with pytest"
pytest -vv --network=local

#######################################################################
Write-Host " "
Write-Host "--------------------------------------------------"
Write-Host "Stopping the local network in wsl"
wsl --% . ~/.local/share/dfx/env; dfx stop

Native build on Windows is temporarily broken..
#######################################################################
Write-Host " "
Write-Host "--------------------------------------------------"
Write-Host "Building the Windows native debug executable with clang++"
icpp build-native --to-compile all
# icpp build-native --to-compile mine

#######################################################################
Write-Host " "
Write-Host "--------------------------------------------------"
Write-Host "Running the Windows native debug executable"
.\build-native\mockic.exe