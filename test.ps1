#######################################################################
# This is a Windows PowerShell script, 
#
# (-) equivalent to the Makefile target `test-all-llms:
# (-) In a Windows PowerShell (*):
#
#     .\test_all_llms.ps1
#
# (*) The Miniconda Powershell is highly recommended
#
#######################################################################

wsl --% dfx identity use default

Write-Host " "
Write-Host "--------------------------------------------------"
Write-Host "Testing icpp_llama2"
Set-Location -Path .\icpp_llama2
.\demo.ps1

# Change directory back to the root
Set-Location -Path ..\..